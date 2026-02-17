#pragma once

#include <Arduino.h>

class LogOutput : public Print {
 public:
  void begin(unsigned long baudRate);
  void setSyslogServer(const String& serverDns);
  const String& getSyslogServer() const;

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;

 private:
  String syslogServerDns_ = "syslog.internal";
  String bufferedLine_;
  IPAddress syslogServerIp_;
  bool syslogServerResolved_ = false;
  unsigned long lastResolveAttemptMs_ = 0;

  void mirrorChar(char c);
  void flushBufferedLine();
  bool ensureSyslogServerResolved();
};

extern LogOutput LogSerial;
