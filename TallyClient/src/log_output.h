#pragma once

#include <Arduino.h>

class LogOutput : public Print {
 public:
  void begin(unsigned long baudRate);
  void flush();
  void setSyslogServer(const String& serverDns);
  void setListenInput(uint8_t listenInput);
  const String& getSyslogServer() const;

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  static constexpr size_t MAX_BUFFERED_LOG_LINES = 32;

  String syslogServerDns_ = "syslog.internal";
  String bufferedLine_;
  String queuedLines_[MAX_BUFFERED_LOG_LINES];
  IPAddress syslogServerIp_;
  uint8_t listenInput_ = 255;
  size_t queuedHead_ = 0;
  size_t queuedCount_ = 0;
  bool syslogServerResolved_ = false;
  unsigned long lastResolveAttemptMs_ = 0;

  void mirrorChar(char c);
  void enqueueLine(const String& line);
  void flushQueuedLines();
  bool sendLine(const String& line);
  void flushBufferedLine();
  bool ensureSyslogServerResolved();
};

extern LogOutput LogSerial;
