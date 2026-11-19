// rs485drv.cpp:
//
// This file is part of the VSCP (https://www.vscp.org)
//
// The MIT License (MIT)
//
// Copyright © 2000-2026 Ake Hedman and contributors, the VSCP Project
// <info@vscp.org>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// ----------------------------------------------------------------------------
//  VSCP Level II driver for RS-485/RS-422 (bus master). See rs485drv.h and
//  docs/README.md.
// ----------------------------------------------------------------------------

#ifdef WIN32
#include "StdAfx.h"
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifndef WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <vscp.h>
#include <vscphelper.h>

#include "spdlog/async.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include "rs485drv.h"

// The driver's logger is the default spdlog logger of the library. When an
// instance is closed it is replaced by one without sinks, so a later
// instance in the same process (a driver restart, a failed open followed by
// a new one) can log and set up its own. spdlog::shutdown() is not used: it
// would destroy the thread pool another instance may still use.
static void
releaseLogger(void)
{
  spdlog::drop("logger");
  spdlog::drop("vscpl2drv-rs485-idle");
  spdlog::set_default_logger(std::make_shared<spdlog::logger>("vscpl2drv-rs485-idle"));
}

// Forward declaration of worker thread
#ifdef WIN32
DWORD WINAPI
workerThread(LPVOID pData);
#else
void *
workerThread(void *pData);
#endif

////////////////////////////////////////////////////////////////////////////////
// Constructor
//

CRs485Drv::CRs485Drv(void)
{
  m_bQuit = false;

#ifdef WIN32
  // Initialize Windows sockets
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    spdlog::error("WSAStartup failed: {0}", iResult);
  }
#endif

  vscp_clearVSCPFilter(&m_rxfilter); // Accept all events

#ifdef WIN32
  m_workerThread = NULL;
#else
  m_workerThread = 0;
#endif
  m_bDebug       = false;
  m_bWriteEnable = false;
  m_maxQueueSize = MAX_ITEMS_IN_QUEUE;

// Initialise semaphores
#ifdef WIN32
  m_semSendQueue    = CreateSemaphore(NULL, 0, MAX_ITEMS_IN_QUEUE, NULL);
  m_semReceiveQueue = CreateSemaphore(NULL, 0, MAX_ITEMS_IN_QUEUE, NULL);
#else
  sem_init(&m_semSendQueue, 0, 0);
  sem_init(&m_semReceiveQueue, 0, 0);
#endif

  // Initialise mutexes
  pthread_mutex_init(&m_mutexSendQueue, NULL);
  pthread_mutex_init(&m_mutexReceiveQueue, NULL);

  // Setting up logging defaults
  m_bConsoleLogEnable = true;
  m_consoleLogLevel   = spdlog::level::info;
  m_consoleLogPattern = "[vscpl2drv-rs485: %c] [%^%l%$] %v";

  m_bEnableFileLog = false;
  m_fileLogLevel   = spdlog::level::info;
  m_fileLogPattern = "[vscpl2drv-rs485: %c] [%^%l%$] %v";
#ifdef WIN32
  m_path_to_log_file = "vscpl2drv-rs485.log";
#else
  m_path_to_log_file = "/tmp/vscpl2drv-rs485.log";
#endif
  m_max_log_size  = 5242880; // 5 MB
  m_max_log_files = 7;

  // Init logging pool
  if (nullptr == spdlog::thread_pool()) {
    spdlog::init_thread_pool(8192, 1);
  }

  // Flush log every five seconds
  spdlog::flush_every(std::chrono::seconds(5));

  ////////////////////////////////////////////////////////
  //      Log files are configured in doLoadConfig      //
  ////////////////////////////////////////////////////////

  // RS-485 defaults (see docs/configure.md)
  m_portOpt.baudrate = 115200;
  m_portOpt.nineBit  = true;
  CRs485Master::parseAddresses("1-127", m_masterCfg.addresses);
}

////////////////////////////////////////////////////////////////////////////////
// Destructor
//

CRs485Drv::~CRs485Drv(void)
{
  close();

#ifdef WIN32
  CloseHandle(m_semSendQueue);
  CloseHandle(m_semReceiveQueue);
#else
  sem_destroy(&m_semSendQueue);
  sem_destroy(&m_semReceiveQueue);
#endif

  pthread_mutex_destroy(&m_mutexSendQueue);
  pthread_mutex_destroy(&m_mutexReceiveQueue);

  // Clear send queue
  while (!m_sendQueue.empty()) {
    vscpEvent *pEvent = m_sendQueue.front();
    m_sendQueue.pop_front();
    vscp_deleteEvent_v2(&pEvent);
  }

  // Clear receive queue
  while (!m_receiveQueue.empty()) {
    vscpEvent *pEvent = m_receiveQueue.front();
    m_receiveQueue.pop_front();
    vscp_deleteEvent_v2(&pEvent);
  }

#ifdef WIN32
  // Cleanup Windows sockets
  WSACleanup();
#endif
}

////////////////////////////////////////////////////////////////////////////////
// open
//

int
CRs485Drv::open(std::string &path, const uint8_t *pguid)
{
  int rv;

  // Must have a valid GUID
  if (NULL == pguid) {
    return VSCP_ERROR_PARAMETER;
  }

  // Set GUID
  m_guid.getFromArray(pguid);

  // Save path for config file
  m_path = path;

  // Read configuration file
  if (VSCP_ERROR_SUCCESS != (rv = doLoadConfig(path))) {
    spdlog::error("[vscpl2drv-rs485] Failed to load configuration file.");
    return rv;
  }

  // Open the serial port and set up the bus master
  if (m_portOpt.device.empty()) {
    spdlog::error("[vscpl2drv-rs485] No serial port ('device') in the configuration.");
    return VSCP_ERROR_PARAMETER;
  }
  if (!m_port.open(m_portOpt)) {
    spdlog::error("[vscpl2drv-rs485] {}", m_port.error());
    return VSCP_ERROR_INIT_FAIL;
  }
  m_master.reset(new CRs485Master(
    m_port,
    m_masterCfg,
    [this](const vscp_rs485_frame_t &frame) {
      vscpEventEx ex;
      frameToEvent(frame, ex);
      addEvent2ReceiveQueue(ex);
    },
    [](uint8_t address, bool active) {
      spdlog::info("[vscpl2drv-rs485] Node {} {}", address, active ? "found" : "lost");
    }));
  spdlog::info("[vscpl2drv-rs485] {} opened, {} baud, {} addresses, {}",
               m_portOpt.device,
               m_portOpt.baudrate,
               m_masterCfg.addresses.size(),
               m_portOpt.nineBit ? "ninth bit" : "8N1");

  // Start the worker thread
  if (VSCP_ERROR_SUCCESS != start()) {
    spdlog::critical("[vscpl2drv-rs485] Failed to start driver.");
    releaseLogger();
    return VSCP_ERROR_INIT_FAIL;
  }

  // Everything is OK
  m_bQuit = false;

  return VSCP_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// close
//

int
CRs485Drv::close(void)
{
  // Do nothing if already terminated
  if (m_bQuit) {
    releaseLogger();
    return VSCP_ERROR_SUCCESS;
  }

  // Stop the worker thread
  stop();

  m_master.reset();
  m_port.close();

  releaseLogger();

  return VSCP_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// doLoadConfig
//

int
CRs485Drv::doLoadConfig(std::string &path)
{
  // Take away possible whitespace
  vscp_trim(path);

  // If first character of path is "{" we expect path to be a JSON
  // configuration string for us to parse
  if ('{' == path.at(0)) {
    spdlog::info("Configuration appears to be a JSON string.");
    try {
      m_j_config = json::parse(path);
    }
    catch (json::parse_error) {
      spdlog::critical("Failed to parse JSON configuration.");
      return VSCP_ERROR_PARSING;
    }
    catch (std::exception &e) {
      spdlog::critical("Failed to parse configuration. Exception='{}'", e.what());
      return VSCP_ERROR_PARSING;
    }
    catch (...) {
      spdlog::critical("Failed to parse configuration due to unknown error.");
      return VSCP_ERROR_PARSING;
    }
  }
  // If path is a file read it
  else if (vscp_fileExists(path)) {
    spdlog::info("Configuration appears to be a file.");
    try {
      std::ifstream in(path, std::ifstream::in);
      in >> m_j_config;
    }
    catch (json::parse_error) {
      spdlog::critical("Failed to parse JSON configuration.");
      return VSCP_ERROR_PARSING;
    }
    catch (std::exception &e) {
      spdlog::critical("Failed to parse configuration. Exception='{}'", e.what());
      return VSCP_ERROR_PARSING;
    }
    catch (...) {
      spdlog::critical("Failed to parse configuration due to unknown error.");
      return VSCP_ERROR_PARSING;
    }
  }
  else {
    // Configuration file does not exist
    return VSCP_ERROR_PARAMETER;
  }

  // Enable extra debug output
  if (m_j_config.contains("debug")) {
    try {
      m_bDebug = m_j_config["debug"].get<bool>();
    }
    catch (const std::exception &ex) {
      spdlog::error("ReadConfig: Failed to read 'debug' Error='{0}'", ex.what());
    }
    catch (...) {
      spdlog::error("ReadConfig: Failed to read 'debug' due to unknown error.");
    }
  }
  else {
    spdlog::warn("ReadConfig: Failed to read 'debug' Defaults will be used.");
  }

  // write
  if (m_j_config.contains("write")) {
    try {
      m_bWriteEnable = m_j_config["write"].get<bool>();
    }
    catch (const std::exception &ex) {
      spdlog::error("ReadConfig: Failed to read 'write' Error='{0}'", ex.what());
    }
    catch (...) {
      spdlog::error("ReadConfig: Failed to read 'write' due to unknown error.");
    }
  }
  else {
    spdlog::warn("ReadConfig: Failed to read 'write' Defaults will be used.");
  }

  // Receive filter
  if (m_j_config.contains("rx-filter")) {
    try {
      std::string filter = m_j_config["rx-filter"].get<std::string>();
      if (!vscp_readFilterFromString(&m_rxfilter, filter.c_str())) {
        spdlog::error("ReadConfig: Failed to parse 'rx-filter' filter='{}'", filter);
      }
    }
    catch (const std::exception &ex) {
      spdlog::error("ReadConfig: Failed to read 'rx-filter' Error='{}'", ex.what());
    }
    catch (...) {
      spdlog::error("ReadConfig: Failed to read 'rx-filter' due to unknown error.");
    }
  }
  else {
    spdlog::warn("ReadConfig: Failed to read 'rx-filter' Defaults will be used.");
  }

  // Max queue size
  if (m_j_config.contains("max-queue-size")) {
    try {
      m_maxQueueSize = m_j_config["max-queue-size"].get<uint16_t>();
    }
    catch (const std::exception &ex) {
      spdlog::error("ReadConfig: Failed to read 'max-queue-size' Error='{}'", ex.what());
    }
    catch (...) {
      spdlog::error("ReadConfig: Failed to read 'max-queue-size' due to unknown error.");
    }
  }

  ///////////////////////////////////////////////////////////////////////////
  //                              RS-485
  ///////////////////////////////////////////////////////////////////////////

  try {
    if (m_j_config.contains("device")) {
      m_portOpt.device = m_j_config["device"].get<std::string>();
    }
    if (m_j_config.contains("baudrate")) {
      m_portOpt.baudrate = m_j_config["baudrate"].get<uint32_t>();
    }
    if (m_j_config.contains("nine-bit")) {
      m_portOpt.nineBit = m_j_config["nine-bit"].get<bool>();
    }
    if (m_j_config.contains("kernel-rs485")) {
      m_portOpt.kernelRs485 = m_j_config["kernel-rs485"].get<bool>();
    }
    if (m_j_config.contains("rts-direction")) {
      m_portOpt.rtsDirection = m_j_config["rts-direction"].get<bool>();
    }
    if (m_j_config.contains("addresses")) {
      const std::string str = m_j_config["addresses"].get<std::string>();
      if (!CRs485Master::parseAddresses(str, m_masterCfg.addresses)) {
        spdlog::critical("ReadConfig: invalid 'addresses' '{}' (e.g. \"1-32,40\")", str);
        return VSCP_ERROR_PARAMETER;
      }
    }
    if (m_j_config.contains("poll-timeout")) {
      m_masterCfg.pollTimeout = m_j_config["poll-timeout"].get<uint32_t>();
    }
    if (m_j_config.contains("discovery-rounds")) {
      m_masterCfg.discoveryRounds = m_j_config["discovery-rounds"].get<uint32_t>();
    }
    if (m_j_config.contains("miss-limit")) {
      m_masterCfg.missLimit = m_j_config["miss-limit"].get<uint32_t>();
    }
    if (m_j_config.contains("fixed-length")) {
      m_masterCfg.codec.fixed = m_j_config["fixed-length"].get<bool>() ? 1 : 0;
    }
    if (m_j_config.contains("crc")) {
      std::string crc = m_j_config["crc"].get<std::string>();
      vscp_makeLower(crc);
      if (("dow" == crc) || ("maxim" == crc)) {
        m_masterCfg.codec.crc = VSCP_RS485_CRC_DOW;
      }
      else if (("0x07" == crc) || ("atm" == crc)) {
        m_masterCfg.codec.crc = VSCP_RS485_CRC_ATM;
      }
      else {
        spdlog::critical("ReadConfig: invalid 'crc' '{}' (\"dow\" or \"0x07\")", crc);
        return VSCP_ERROR_PARAMETER;
      }
    }
    if (m_j_config.contains("broadcast")) {
      m_masterCfg.broadcast = m_j_config["broadcast"].get<bool>();
    }
  }
  catch (const std::exception &ex) {
    spdlog::critical("ReadConfig: invalid RS-485 setting. Error='{}'", ex.what());
    return VSCP_ERROR_PARAMETER;
  }
  if ((0 == m_portOpt.baudrate) || (0 == m_masterCfg.pollTimeout) || (0 == m_masterCfg.missLimit)) {
    spdlog::critical("ReadConfig: 'baudrate', 'poll-timeout' and 'miss-limit' must be above zero");
    return VSCP_ERROR_PARAMETER;
  }
  m_masterCfg.txQueueSize = m_maxQueueSize;
  // Between characters of a frame: three character times, at least 2 ms
  const uint32_t charUs   = 11u * 1000000u / m_portOpt.baudrate;
  const uint32_t charMs   = (3 * charUs + 999) / 1000;
  m_masterCfg.charTimeout = (charMs < 2) ? 2 : charMs;

  ///////////////////////////////////////////////////////////////////////////
  //                             Logging
  ///////////////////////////////////////////////////////////////////////////

  if (m_j_config.contains("logging") && m_j_config["logging"].is_object()) {

    json j = m_j_config["logging"];

    // Logging: file-enable-log
    if (j.contains("file-enable-log")) {
      try {
        m_bEnableFileLog = j["file-enable-log"].get<bool>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-enable-log' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-enable-log' due to unknown error.");
      }
    }

    // Logging: file-log-level
    if (j.contains("file-log-level")) {
      std::string str;
      try {
        str = j["file-log-level"].get<std::string>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-log-level' Error='{0}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-log-level' due to unknown error.");
      }
      vscp_makeLower(str);
      if (std::string::npos != str.find("off")) {
        m_fileLogLevel = spdlog::level::off;
      }
      else if (std::string::npos != str.find("critical")) {
        m_fileLogLevel = spdlog::level::critical;
      }
      else if (std::string::npos != str.find("err")) {
        m_fileLogLevel = spdlog::level::err;
      }
      else if (std::string::npos != str.find("warn")) {
        m_fileLogLevel = spdlog::level::warn;
      }
      else if (std::string::npos != str.find("info")) {
        m_fileLogLevel = spdlog::level::info;
      }
      else if (std::string::npos != str.find("debug")) {
        m_fileLogLevel = spdlog::level::debug;
      }
      else if (std::string::npos != str.find("trace")) {
        m_fileLogLevel = spdlog::level::trace;
      }
      else {
        spdlog::error("ReadConfig: LOGGING 'file-log-level' has invalid value [{}]. Default value used.", str);
      }
    }

    // Logging: file-pattern
    if (j.contains("file-pattern")) {
      try {
        m_fileLogPattern = j["file-pattern"].get<std::string>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-pattern' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-pattern' due to unknown error.");
      }
    }

    // Logging: file-path
    if (j.contains("file-path")) {
      try {
        m_path_to_log_file = j["file-path"].get<std::string>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-path' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-path' due to unknown error.");
      }
    }

    // Logging: file-max-size
    if (j.contains("file-max-size")) {
      try {
        m_max_log_size = j["file-max-size"].get<uint32_t>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-max-size' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-max-size' due to unknown error.");
      }
    }

    // Logging: file-max-files
    if (j.contains("file-max-files")) {
      try {
        m_max_log_files = j["file-max-files"].get<uint16_t>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'file-max-files' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'file-max-files' due to unknown error.");
      }
    }

    // Logging: console-enable-log
    if (j.contains("console-enable-log")) {
      try {
        m_bConsoleLogEnable = j["console-enable-log"].get<bool>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'console-enable-log' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'console-enable-log' due to unknown error.");
      }
    }

    // Logging: console-log-level
    if (j.contains("console-log-level")) {
      std::string str;
      try {
        str = j["console-log-level"].get<std::string>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'console-log-level' Error='{0}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'console-log-level' due to unknown error.");
      }
      vscp_makeLower(str);
      if (std::string::npos != str.find("off")) {
        m_consoleLogLevel = spdlog::level::off;
      }
      else if (std::string::npos != str.find("critical")) {
        m_consoleLogLevel = spdlog::level::critical;
      }
      else if (std::string::npos != str.find("err")) {
        m_consoleLogLevel = spdlog::level::err;
      }
      else if (std::string::npos != str.find("warn")) {
        m_consoleLogLevel = spdlog::level::warn;
      }
      else if (std::string::npos != str.find("info")) {
        m_consoleLogLevel = spdlog::level::info;
      }
      else if (std::string::npos != str.find("debug")) {
        m_consoleLogLevel = spdlog::level::debug;
      }
      else if (std::string::npos != str.find("trace")) {
        m_consoleLogLevel = spdlog::level::trace;
      }
      else {
        spdlog::error("ReadConfig: LOGGING 'console-log-level' has invalid value [{}]. Default value used.", str);
      }
    }

    // Logging: console-pattern
    if (j.contains("console-pattern")) {
      try {
        m_consoleLogPattern = j["console-pattern"].get<std::string>();
      }
      catch (const std::exception &ex) {
        spdlog::error("ReadConfig: Failed to read 'console-pattern' Error='{}'", ex.what());
      }
      catch (...) {
        spdlog::error("ReadConfig: Failed to read 'console-pattern' due to unknown error.");
      }
    }

  } // Logging
  else {
    spdlog::info("ReadConfig: No logging has been setup.");
  }

  ///////////////////////////////////////////////////////////////////////////
  //                          Setup logger
  ///////////////////////////////////////////////////////////////////////////

  // Console log
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

  if (m_bConsoleLogEnable) {
    console_sink->set_level(m_consoleLogLevel);
    console_sink->set_pattern(m_consoleLogPattern);
  }
  else {
    // If disabled set to off
    console_sink->set_level(spdlog::level::off);
  }

  auto rotating_file_sink =
    std::make_shared<spdlog::sinks::rotating_file_sink_mt>(m_path_to_log_file.c_str(), m_max_log_size, m_max_log_files);

  if (m_bEnableFileLog) {
    rotating_file_sink->set_level(m_fileLogLevel);
    rotating_file_sink->set_pattern(m_fileLogPattern);
  }
  else {
    // If disabled set to off
    rotating_file_sink->set_level(spdlog::level::off);
  }

  std::vector<spdlog::sink_ptr> sinks{ console_sink, rotating_file_sink };
  auto logger = std::make_shared<spdlog::async_logger>("logger",
                                                       sinks.begin(),
                                                       sinks.end(),
                                                       spdlog::thread_pool(),
                                                       spdlog::async_overflow_policy::block);
  spdlog::drop("logger"); // From an earlier configuration of this or another instance
  spdlog::register_logger(logger);
  spdlog::set_default_logger(logger);

  spdlog::debug("Read configuration.");

  return VSCP_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// doSaveConfig
//

int
CRs485Drv::doSaveConfig(void)
{
  if (m_j_config.value("write", false)) {
    // The driver has no settings that change at run time: nothing to write.
  }
  return VSCP_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// start
//

int
CRs485Drv::start(void)
{
  spdlog::debug("[vscpl2drv-rs485] Starting worker thread...");

  m_bQuit = false;

#ifdef WIN32
  m_workerThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) workerThread, this, 0, NULL);
  if (m_workerThread == NULL) {
    spdlog::error("[vscpl2drv-rs485] Unable to start the worker thread.");
    return VSCP_ERROR_ERROR;
  }
#else
  if (pthread_create(&m_workerThread, NULL, workerThread, this)) {
    spdlog::error("[vscpl2drv-rs485] Unable to start the worker thread.");
    return VSCP_ERROR_ERROR;
  }
#endif

  return VSCP_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// stop
//

int
CRs485Drv::stop(void)
{
  // Tell the thread it's time to quit
  m_bQuit = true;

  spdlog::debug("[vscpl2drv-rs485] Terminating worker thread.");

// Wait for thread to finish and clean up
#ifdef WIN32
  if (m_workerThread == NULL) { // Check if thread was created
    // Not started
    return VSCP_ERROR_SUCCESS;
  }
  WaitForSingleObject(m_workerThread, INFINITE);
  CloseHandle(m_workerThread);
  m_workerThread = NULL; // Reset thread handle
#else
  if (!m_workerThread) { // Check if thread was created
    // Not started
    return VSCP_ERROR_SUCCESS;
  }
  pthread_join(m_workerThread, NULL);
  m_workerThread = 0; // Reset thread ID
#endif

  spdlog::debug("[vscpl2drv-rs485] Terminated worker thread.");

  return VSCP_ERROR_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
// restart
//

int
CRs485Drv::restart(void)
{
  if (VSCP_ERROR_SUCCESS != stop()) {
    spdlog::warn("[vscpl2drv-rs485] Failed to stop driver.");
  }

  if (VSCP_ERROR_SUCCESS != start()) {
    spdlog::warn("[vscpl2drv-rs485] Failed to start driver.");
  }

  return VSCP_ERROR_SUCCESS;
}

////////////////////////////////////////////////////////////////////
// addEvent2SendQueue
//
//  Add event to send queue (host --> driver --> device)
//

int
CRs485Drv::addEvent2SendQueue(const vscpEvent *pEvent)
{
  pthread_mutex_lock(&m_mutexSendQueue);
  if (m_maxQueueSize && (m_sendQueue.size() >= m_maxQueueSize)) {
    pthread_mutex_unlock(&m_mutexSendQueue);
    spdlog::warn("[vscpl2drv-rs485] Send queue full, dropping event.");
    return VSCP_ERROR_FIFO_FULL;
  }
  m_sendQueue.push_back((vscpEvent *) pEvent);
#ifdef WIN32
  ReleaseSemaphore(m_semSendQueue, 1, NULL);
#else
  sem_post(&m_semSendQueue);
#endif
  pthread_mutex_unlock(&m_mutexSendQueue);
  return VSCP_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// addEvent2ReceiveQueue
//
//  Add event to receive queue (device --> driver --> host)
//

int
CRs485Drv::addEvent2ReceiveQueue(const vscpEvent *pEvent)
{
  pthread_mutex_lock(&m_mutexReceiveQueue);
  if (m_maxQueueSize && (m_receiveQueue.size() >= m_maxQueueSize)) {
    pthread_mutex_unlock(&m_mutexReceiveQueue);
    spdlog::warn("[vscpl2drv-rs485] Receive queue full, dropping event.");
    return VSCP_ERROR_FIFO_FULL;
  }
  m_receiveQueue.push_back((vscpEvent *) pEvent);
  pthread_mutex_unlock(&m_mutexReceiveQueue);
#ifdef WIN32
  ReleaseSemaphore(m_semReceiveQueue, 1, NULL);
#else
  sem_post(&m_semReceiveQueue);
#endif
  return VSCP_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// addEvent2ReceiveQueue
//

int
CRs485Drv::addEvent2ReceiveQueue(vscpEventEx &ex)
{
  vscpEvent *pev = new vscpEvent();
  if (NULL == pev) {
    spdlog::error("[vscpl2drv-rs485] addEvent2ReceiveQueue - Unable to allocate event storage.");
    return VSCP_ERROR_MEMORY;
  }

  pev->pdata = NULL;

  if (!vscp_convertEventExToEvent(pev, &ex)) {
    spdlog::error("[vscpl2drv-rs485] Failed to convert event from ex to ev.");
    vscp_deleteEvent(pev);
    return VSCP_ERROR_ERROR;
  }

  if (vscp_doLevel2Filter(pev, &m_rxfilter)) {
    return addEvent2ReceiveQueue(pev);
  }
  else {
    vscp_deleteEvent(pev);
  }

  return VSCP_ERROR_SUCCESS;
}

//////////////////////////////////////////////////////////////////////
// frameToEvent
//

void
CRs485Drv::frameToEvent(const vscp_rs485_frame_t &frame, vscpEventEx &ex)
{
  memset(&ex, 0, sizeof(ex));
  // RS-485 addresses are set in the node: hard coded nickname
  ex.head      = VSCP_PRIORITY_NORMAL | VSCP_HEADER_HARD_CODED;
  ex.timestamp = vscp_makeTimeStamp();
  vscp_setEventExToNow(&ex);
  memcpy(ex.GUID, m_guid.getGUID(), 16);
  ex.GUID[14]   = 0;
  ex.GUID[15]   = frame.src; // Nickname = RS-485 address
  ex.vscp_class = frame.vscp_class;
  ex.vscp_type  = frame.vscp_type;
  ex.sizeData   = frame.size;
  memcpy(ex.data, frame.data, frame.size);
}

//////////////////////////////////////////////////////////////////////
// eventToFrame
//

bool
CRs485Drv::eventToFrame(const vscpEvent *pEvent, vscp_rs485_frame_t &frame, int &target)
{
  memset(&frame, 0, sizeof(frame));
  frame.op             = VSCP_RS485_OP_EVENT;
  const uint8_t *pdata = pEvent->pdata;
  uint16_t size        = pEvent->sizeData;
  uint16_t vscpClass   = pEvent->vscp_class;
  bool chosen          = false;

  if ((vscpClass >= 512) && (vscpClass < 1024)) {
    // Level I event in Level II form: the destination GUID first
    if ((size < 16) || (NULL == pdata)) {
      return false;
    }
    if ((0 != memcmp(pdata, m_guid.getGUID(), 14)) || (0 == pdata[15])) {
      return false; // Not for a node behind this driver
    }
    target = pdata[15];
    chosen = true;
    pdata += 16;
    size = (uint16_t) (size - 16);
    vscpClass = (uint16_t) (vscpClass - 512);
  }
  else if (vscpClass >= 512) {
    return false; // Level II only class
  }

  if (size > 8) {
    return false;
  }
  frame.vscp_class = vscpClass;
  frame.vscp_type  = (uint8_t) pEvent->vscp_type;
  frame.size       = (uint8_t) size;
  if (size) {
    memcpy(frame.data, pdata, size);
  }
  if (!chosen) {
    target = CRs485Master::targetOf(frame.vscp_class, frame.vscp_type, frame.data, frame.size);
  }
  return true;
}

//////////////////////////////////////////////////////////////////////
// takeSendEvent
//

vscpEvent *
CRs485Drv::takeSendEvent(void)
{
#ifdef WIN32
  if (WAIT_OBJECT_0 != WaitForSingleObject(m_semSendQueue, 0)) {
    return NULL;
  }
#else
  if (0 != sem_trywait(&m_semSendQueue)) {
    return NULL;
  }
#endif
  vscpEvent *pEvent = NULL;
  pthread_mutex_lock(&m_mutexSendQueue);
  if (!m_sendQueue.empty()) {
    pEvent = m_sendQueue.front();
    m_sendQueue.pop_front();
  }
  pthread_mutex_unlock(&m_mutexSendQueue);
  return pEvent;
}

///////////////////////////////////////////////////////////////////////////////
//                           Worker thread
///////////////////////////////////////////////////////////////////////////////

//
// The worker thread is the heart of the driver. It is responsible for
// transferring events between the host application and the device/service
// this driver interfaces.
//
//  * Events from the host shows up in the send queue (m_sendQueue). Deliver
//    them to your device.
//
//  * Data from your device should be converted to VSCP events and handed
//    to the host with addEvent2ReceiveQueue().
//

#ifdef WIN32
DWORD WINAPI
workerThread(LPVOID pData)
#else
void *
workerThread(void *pData)
#endif
{
  CRs485Drv *pDrv = (CRs485Drv *) pData;
  if (NULL == pDrv) {
    spdlog::error("[vscpl2drv-rs485] workerThread: Invalid CRs485Drv pointer.");
#ifdef WIN32
    return 0;
#else
    return NULL;
#endif
  }

  spdlog::info("[vscpl2drv-rs485] Worker thread started.");

  auto lastStats = std::chrono::steady_clock::now();

  while (!pDrv->m_bQuit) {

    // Events from the host go to the bus master's transmit queue
    vscpEvent *pEvent;
    while (NULL != (pEvent = pDrv->takeSendEvent())) {
      vscp_rs485_frame_t frame;
      int target = -1;
      if (pDrv->eventToFrame(pEvent, frame, target)) {
        if (!pDrv->m_master->queueEvent(frame, target)) {
          spdlog::warn("[vscpl2drv-rs485] Transmit queue full, event dropped.");
        }
        else if (pDrv->m_bDebug) {
          spdlog::debug("[vscpl2drv-rs485] Event class={} type={} to {}",
                        frame.vscp_class,
                        frame.vscp_type,
                        (target < 0) ? std::string("all nodes") : std::to_string(target));
        }
      }
      else if (pDrv->m_bDebug) {
        spdlog::debug("[vscpl2drv-rs485] Event class={} type={} can not go on a Level I bus, ignored.",
                      pEvent->vscp_class,
                      pEvent->vscp_type);
      }
      vscp_deleteEvent_v2(&pEvent);
    }

    // One bus transaction: send a host event, poll a node or probe for a new one
    if (!pDrv->m_master->step()) {
#ifdef WIN32
      Sleep(10);
#else
      usleep(10000);
#endif
    }

    // Bus statistics in the log once a minute
    if (std::chrono::steady_clock::now() - lastStats > std::chrono::seconds(60)) {
      lastStats                 = std::chrono::steady_clock::now();
      const Rs485MasterStats st = pDrv->m_master->statistics();
      spdlog::debug("[vscpl2drv-rs485] polls={} answers={} events={} sent={} timeouts={} crc={} frame={} "
                    "wrong-sender={} nodes={}",
                    st.polls,
                    st.answers,
                    st.events,
                    st.sent,
                    st.timeouts,
                    st.crcErrors,
                    st.frameErrors,
                    st.wrongSender,
                    pDrv->m_master->activeNodes().size());
    }
  }

  spdlog::info("[vscpl2drv-rs485] Worker thread terminated.");

#ifdef WIN32
  return 0;
#else
  return NULL;
#endif
}
