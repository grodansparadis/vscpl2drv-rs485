// test-driver.cpp
//
// Tests the built Level II driver the way the VSCP daemon uses it: the
// shared library is loaded at run time and VSCPOpen/VSCPRead/VSCPWrite/
// VSCPClose are looked up by name. The driver runs a real RS-485 segment
// against simulated nodes at the other end of a pseudo terminal (8N1: a
// pseudo terminal has no parity bit).
//
//   test-driver path/to/libvscpl2drv-rs485.so
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, the VSCP Project <info@vscp.org>
//

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <canal.h>
#include <vscp.h>

#include "vscp-rs485.h"

typedef long (*fnOpen)(const char *, const char *);
typedef int (*fnClose)(long);
typedef int (*fnWrite)(long, const vscpEvent *, unsigned long);
typedef int (*fnRead)(long, vscpEvent *, unsigned long);
typedef unsigned long (*fnVersion)(void);
typedef const char *(*fnVendor)(void);

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    g_checks++;                                                                                                        \
    if (!(cond)) {                                                                                                     \
      g_failed++;                                                                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                      \
    }                                                                                                                  \
  } while (0)

// The nodes of the segment: 5 and 9 answer polls with a queued event or
// NO_EVENTS, and record the events the master sends
class SimSegment {
public:
  bool start(void)
  {
    m_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if ((m_fd < 0) || grantpt(m_fd) || unlockpt(m_fd)) {
      return false;
    }
    m_slave  = ptsname(m_fd);
    m_run    = true;
    m_thread = std::thread(&SimSegment::run, this);
    return true;
  }
  void stop(void)
  {
    m_run = false;
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_fd >= 0) {
      ::close(m_fd);
    }
  }
  std::string slavePath(void) const { return m_slave; }
  void queueEvent(uint8_t node, uint16_t cls, uint8_t type, std::vector<uint8_t> data)
  {
    vscp_rs485_frame_t f;
    std::memset(&f, 0, sizeof(f));
    f.src        = node;
    f.op         = VSCP_RS485_OP_EVENT;
    f.vscp_class = cls;
    f.vscp_type  = type;
    f.size       = (uint8_t) data.size();
    if (!data.empty()) {
      std::memcpy(f.data, data.data(), data.size());
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events[node].push_back(f);
  }
  int pollsOf(uint8_t node)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_polls[node];
  }
  std::vector<vscp_rs485_frame_t> received(void)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_received;
  }

private:
  void run(void)
  {
    const vscp_rs485_options_t opt = { VSCP_RS485_CRC_DOW, 0 };
    vscp_rs485_rx_t rx;
    vscp_rs485_rx_init(&rx, &opt);
    while (m_run) {
      struct pollfd pfd = { m_fd, POLLIN, 0 };
      if (::poll(&pfd, 1, 20) <= 0) {
        vscp_rs485_rx_reset(&rx);
        continue;
      }
      uint8_t buf[64];
      const ssize_t n = ::read(m_fd, buf, sizeof(buf));
      for (ssize_t i = 0; i < n; i++) {
        vscp_rs485_frame_t f;
        if (VSCP_RS485_RX_FRAME != vscp_rs485_rx_char(&rx, buf[i], -1, &f)) {
          continue;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (VSCP_RS485_OP_EVENT == f.op) {
          m_received.push_back(f);
          continue;
        }
        if ((VSCP_RS485_OP_POLL != f.op) || ((5 != f.dest) && (9 != f.dest))) {
          continue;
        }
        m_polls[f.dest]++;
        vscp_rs485_frame_t answer;
        std::memset(&answer, 0, sizeof(answer));
        if (!m_events[f.dest].empty()) {
          answer = m_events[f.dest].front();
          m_events[f.dest].erase(m_events[f.dest].begin());
        }
        else {
          answer.src = f.dest;
          answer.op  = VSCP_RS485_OP_NO_EVENTS;
        }
        uint8_t out[VSCP_RS485_MAX_FRAME];
        const size_t len = vscp_rs485_encode(&opt, &answer, out);
        if (::write(m_fd, out, len) != (ssize_t) len) {
          continue;
        }
      }
    }
  }

  int m_fd = -1;
  std::string m_slave;
  std::atomic<bool> m_run{ false };
  std::thread m_thread;
  std::mutex m_mutex;
  std::map<uint8_t, std::vector<vscp_rs485_frame_t>> m_events;
  std::map<uint8_t, int> m_polls;
  std::vector<vscp_rs485_frame_t> m_received;
};

static const uint8_t GUID[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC,
                                  0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00 };

static vscpEvent
event(uint16_t cls, uint16_t type, std::vector<uint8_t> &data)
{
  vscpEvent ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.head       = VSCP_PRIORITY_NORMAL;
  ev.vscp_class = cls;
  ev.vscp_type  = type;
  ev.sizeData   = (uint16_t) data.size();
  ev.pdata      = data.empty() ? nullptr : data.data();
  return ev;
}

static void
waitFor(const std::function<bool(void)> &done)
{
  for (int i = 0; (i < 150) && !done(); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

int
main(int argc, char **argv)
{
  if (argc < 2) {
    std::printf("Usage: %s path-to-driver\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    std::printf("FAIL: unable to load %s: %s\n", argv[1], dlerror());
    return 1;
  }
  fnOpen open       = (fnOpen) dlsym(lib, "VSCPOpen");
  fnClose close     = (fnClose) dlsym(lib, "VSCPClose");
  fnWrite write     = (fnWrite) dlsym(lib, "VSCPWrite");
  fnRead read       = (fnRead) dlsym(lib, "VSCPRead");
  fnVersion version = (fnVersion) dlsym(lib, "VSCPGetVersion");
  fnVendor vendor   = (fnVendor) dlsym(lib, "VSCPGetVendorString");
  CHECK(open && close && write && read && version && vendor);
  if (!(open && close && write && read && version && vendor)) {
    return 1;
  }
  CHECK(version() != 0);
  CHECK(std::strlen(vendor()) > 0);

  const std::string quiet = R"("logging": { "console-enable-log": false, "file-enable-log": false })";

  // Invalid configurations
  CHECK(0 == open(("{ \"baudrate\": 115200, " + quiet + " }").c_str(), (const char *) GUID)); // No device
  CHECK(0 == open(("{ \"device\": \"/no/such/port\", " + quiet + " }").c_str(), (const char *) GUID));
  CHECK(0 == open("/no/such/config.json", (const char *) GUID));

  SimSegment seg;
  CHECK(seg.start());
  CHECK(0 == open(("{ \"device\": \"" + seg.slavePath() + "\", \"addresses\": \"0-3\", " + quiet + " }").c_str(),
                  (const char *) GUID));
  CHECK(0 == open(("{ \"device\": \"" + seg.slavePath() + "\", \"crc\": \"md5\", " + quiet + " }").c_str(),
                  (const char *) GUID));

  const std::string config = "{ \"device\": \"" + seg.slavePath() +
                             "\", \"baudrate\": 115200, \"addresses\": \"1-10\", \"poll-timeout\": 30, "
                             "\"nine-bit\": false, \"crc\": \"dow\", " +
                             quiet + " }";
  long h = open(config.c_str(), (const char *) GUID);
  CHECK(h > 0);

  // An event from node 5 becomes a Level II event with the driver GUID
  // and the node address in the last byte
  seg.queueEvent(5, 10, 6, { 0x89, 0x02, 0x1A });
  vscpEvent rx;
  std::memset(&rx, 0, sizeof(rx));
  CHECK(CANAL_ERROR_SUCCESS == read(h, &rx, 3000));
  CHECK((10 == rx.vscp_class) && (6 == rx.vscp_type) && (3 == rx.sizeData));
  CHECK((nullptr != rx.pdata) && (0x89 == rx.pdata[0]) && (0x1A == rx.pdata[2]));
  CHECK(0 == std::memcmp(rx.GUID, GUID, 14));
  CHECK((0 == rx.GUID[14]) && (5 == rx.GUID[15]));
  CHECK((VSCP_PRIORITY_NORMAL | VSCP_HEADER_HARD_CODED) == (rx.head & 0xF0));
  delete[] rx.pdata;
  rx.pdata = nullptr;

  // Class above 255
  seg.queueEvent(9, 0x105, 1, {});
  CHECK(CANAL_ERROR_SUCCESS == read(h, &rx, 3000));
  CHECK((0x105 == rx.vscp_class) && (1 == rx.vscp_type) && (0 == rx.sizeData) && (9 == rx.GUID[15]));
  delete[] rx.pdata;
  rx.pdata = nullptr;
  CHECK(CANAL_ERROR_TIMEOUT == read(h, &rx, 200));

  waitFor([&]() { return (seg.pollsOf(5) >= 3) && (seg.pollsOf(9) >= 3); });

  // A Level I event for everybody goes to both nodes
  std::vector<uint8_t> d1 = { 1, 2, 3 };
  vscpEvent ev            = event(20, 9, d1);
  CHECK(CANAL_ERROR_SUCCESS == write(h, &ev, 0));

  // READ_REGISTER in Level II form (class 512 + 0): destination GUID of
  // node 9 behind this driver, then the Level I data; only node 9 gets it
  std::vector<uint8_t> d2(GUID, GUID + 16);
  d2[15] = 9;
  d2.push_back(9);
  d2.push_back(0xD0);
  ev = event(512, 9, d2);
  CHECK(CANAL_ERROR_SUCCESS == write(h, &ev, 0));

  // Not for this segment: another interface's GUID, a Level II class, too
  // much data for Level I
  std::vector<uint8_t> d3(16, 0x42);
  d3.push_back(1);
  ev = event(512, 9, d3);
  CHECK(CANAL_ERROR_SUCCESS == write(h, &ev, 0));
  std::vector<uint8_t> d4 = { 1 };
  ev                      = event(1040, 6, d4);
  CHECK(CANAL_ERROR_SUCCESS == write(h, &ev, 0));
  std::vector<uint8_t> d5(9, 7);
  ev = event(20, 3, d5);
  CHECK(CANAL_ERROR_SUCCESS == write(h, &ev, 0));

  waitFor([&]() { return seg.received().size() >= 3; });
  std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Nothing more may come
  auto got = seg.received();
  CHECK(3 == got.size());
  int toNode5 = 0, toNode9 = 0, readReg = 0;
  for (const auto &f : got) {
    CHECK(0 == f.src);
    toNode5 += (5 == f.dest);
    toNode9 += (9 == f.dest);
    readReg += (0 == f.vscp_class) && (9 == f.vscp_type) && (9 == f.dest) && (2 == f.size) && (9 == f.data[0]) &&
               (0xD0 == f.data[1]);
  }
  CHECK((1 == toNode5) && (2 == toNode9) && (1 == readReg));

  CHECK(CANAL_ERROR_SUCCESS == close(h));
  seg.stop();

  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
