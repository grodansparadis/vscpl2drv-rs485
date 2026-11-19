// rs485drv.h:
//
// VSCP Level II driver for RS-485/RS-422: the bus master of "VSCP Level I
// over RS-485/RS-422". The bus master, the serial port and the protocol
// (rs485-master.*, rs485-port.*, vscp-rs485.*) are the same files as in
// vscpl1drv-rs485.
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

#if !defined(VSCP_RS485_DRV_H__INCLUDED_)
#define VSCP_RS485_DRV_H__INCLUDED_

// Robust Windows platform detection for GitHub Actions and various build environments
#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__TOS_WIN__) || defined(__WINDOWS__)
#ifndef WIN32
#define WIN32
#endif
#ifndef _WIN32
#define _WIN32
#endif
#endif

#ifdef WIN32
#include "StdAfx.h"
#endif

#include <list>
#include <memory>
#include <string>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#if WIN32
#else
#include <semaphore.h>
#include <syslog.h>
#include <unistd.h>
#endif

#include <time.h>

#include <guid.h>
#include <vscp.h>

#include <nlohmann/json.hpp> // Needs C++11  -std=c++11

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/spdlog.h"

#include "rs485-master.h"
#include "rs485-port.h"

// https://github.com/nlohmann/json
using json = nlohmann::json;

// Maximum number of events in the send queue
const uint16_t MAX_ITEMS_IN_QUEUE = 32000;

#define DRIVER_COPYRIGHT "Copyright © 2000-2026 Ake Hedman, the VSCP Project, https://www.vscp.org"

///////////////////////////////////////////////////////////////////////////////
// CRs485Drv
//
// The main driver class. The VSCP driver API (see vscpl2drv-rs485.cpp)
// forwards all calls to an instance of this class. The worker thread runs
// the RS-485 bus master.
//

class CRs485Drv {

public:
  CRs485Drv(void);
  ~CRs485Drv(void);

  /*!
    @brief Init and start the driver
    @param path Path to configuration file (or JSON configuration string)
    @param pguid Pointer to GUID for the driver
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int open(std::string &path, const uint8_t *pguid);

  /*!
    @brief Close the driver
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int close(void);

  /*!
    @brief Load configuration from file or JSON string
    @param path Path to configuration file or a JSON configuration string
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
            VSCP_ERROR_PARSING if there is a parsing error.
  */
  int doLoadConfig(std::string &path);

  /*!
    @brief Save configuration to file
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int doSaveConfig(void);

  /*!
    @brief Start the driver worker thread
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int start(void);

  /*!
    @brief Stop the driver worker thread
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int stop(void);

  /*!
    @brief Restart the driver worker thread
    @return VSCP_ERROR_SUCCESS if all is OK, otherwise VSCP error code.
  */
  int restart(void);

  /*!
    @brief Add an event to the send queue (host --> driver --> device)

    Events written to the driver by the host application (for example the
    VSCP daemon) end up here. The worker thread picks them up and should
    deliver them to the device/service this driver interfaces.

    @param pEvent Pointer to the VSCP event. The driver takes over ownership
      of the event and will free it when it has been handled.
    @return VSCP_ERROR_SUCCESS on success, otherwise VSCP error code.
  */
  int addEvent2SendQueue(const vscpEvent *pEvent);

  /*!
    @brief Add an event to the receive queue (device --> driver --> host)

    Call this method when the device/service this driver interfaces has
    produced data that should be sent as a VSCP event to the host
    application.

    @param pEvent Pointer to the VSCP event. The driver takes over ownership
      of the event and will free it when it has been handled.
    @return VSCP_ERROR_SUCCESS on success, otherwise VSCP error code.
  */
  int addEvent2ReceiveQueue(const vscpEvent *pEvent);

  /*!
    @brief Add an event ex to the receive queue (device --> driver --> host)
    @param ex Reference to the VSCP event ex
    @return VSCP_ERROR_SUCCESS on success, otherwise VSCP error code.
  */
  int addEvent2ReceiveQueue(vscpEventEx &ex);

  /*!
    @brief Convert an event from a node to a Level II event: the driver GUID
    with the node address in the last byte, priority normal, hard coded
    nickname.
  */
  void frameToEvent(const vscp_rs485_frame_t &frame, vscpEventEx &ex);

  /*!
    @brief Convert an event from the host to an RS-485 event frame.
    Level I events (class < 512, at most eight data bytes) go to every node,
    or to the node a protocol event names. Level I events in Level II form
    (class 512-1023, destination GUID in data bytes 0-15) go to their node
    if it is behind this driver.
    @param target Set to the node address, or -1 for every node
    @return false if the event can not go on the bus
  */
  bool eventToFrame(const vscpEvent *pEvent, vscp_rs485_frame_t &frame, int &target);

  /// Take the next event from the send queue without waiting
  vscpEvent *takeSendEvent(void);

public:
  /// Parsed Config file
  json m_j_config;

  // ------------------------------------------------------------------------

  // * * * Configuration

  /// Path to configuration file
  std::string m_path;

  /// True if config is remote writable
  bool m_bWriteEnable;

  /// Filters events we are not interested in
  vscpEventFilter m_rxfilter;

  /////////////////////////////////////////////////////////
  //                      Logging
  /////////////////////////////////////////////////////////

  bool m_bEnableFileLog;                       // True to enable logging
  spdlog::level::level_enum m_fileLogLevel;    // log level
  std::string m_fileLogPattern;                // log file pattern
  std::string m_path_to_log_file;              // Path to logfile
  uint32_t m_max_log_size;                     // Max size for logfile before rotating occurs
  uint16_t m_max_log_files;                    // Max log files to keep
  bool m_bConsoleLogEnable;                    // True to enable logging to console
  spdlog::level::level_enum m_consoleLogLevel; // log level
  std::string m_consoleLogPattern;             // Console log pattern

  // ------------------------------------------------------------------------

  // * * * RS-485

  Rs485PortOptions m_portOpt;    ///< Serial port (device, baud rate, ninth bit, direction)
  Rs485MasterConfig m_masterCfg; ///< Bus master (addresses, time-outs, frame format)
  CRs485Port m_port;
  std::unique_ptr<CRs485Master> m_master;

  bool m_bQuit;  // Flag to indicate that the worker thread should quit
  bool m_bDebug; // Debug flag (gives extra debug output)
  cguid m_guid;  // Driver GUID

#ifdef WIN32
  HANDLE m_semReceiveQueue; // Semaphore for receive queue
#else
  sem_t m_semReceiveQueue; // Semaphore for receive queue
#endif
  pthread_mutex_t m_mutexReceiveQueue;   // Mutex for receive queue
  std::list<vscpEvent *> m_receiveQueue; // Receive queue

#ifdef WIN32
  HANDLE m_semSendQueue; // Semaphore for send queue
#else
  sem_t m_semSendQueue; // Semaphore for send queue
#endif
  pthread_mutex_t m_mutexSendQueue;   // Mutex for send queue
  std::list<vscpEvent *> m_sendQueue; // Send queue

#ifdef WIN32
  HANDLE m_workerThread; // Worker thread for the driver
#else
  pthread_t m_workerThread; // Worker thread for the driver
#endif

private:
  uint16_t m_maxQueueSize; // Max size of send/receive queues
};

#endif // !defined(VSCP_RS485_DRV_H__INCLUDED_)
