#include "log_output.h"

#include <WiFi.h>
#include <WiFiUdp.h>

namespace {

constexpr uint16_t SYSLOG_PORT = 514;
constexpr unsigned long DNS_RETRY_INTERVAL_MS = 5000UL;
constexpr size_t MAX_BUFFERED_LOG_LINE = 512;

WiFiUDP syslogUdp;

}  // namespace

LogOutput LogSerial;

void LogOutput::begin(unsigned long baudRate) {
  ::Serial.begin(baudRate);
}

void LogOutput::setSyslogServer(const String& serverDns) {
  if (serverDns == syslogServerDns_) {
    return;
  }

  syslogServerDns_ = serverDns;
  syslogServerResolved_ = false;
  lastResolveAttemptMs_ = 0;
}

const String& LogOutput::getSyslogServer() const {
  return syslogServerDns_;
}

size_t LogOutput::write(uint8_t c) {
  const size_t written = ::Serial.write(c);
  mirrorChar(static_cast<char>(c));
  return written;
}

size_t LogOutput::write(const uint8_t* buffer, size_t size) {
  const size_t written = ::Serial.write(buffer, size);
  for (size_t i = 0; i < size; ++i) {
    mirrorChar(static_cast<char>(buffer[i]));
  }
  return written;
}

void LogOutput::mirrorChar(char c) {
  if (bufferedLine_.length() < MAX_BUFFERED_LOG_LINE && c != '\n' && c != '\r') {
    bufferedLine_ += c;
  } else {
    // If the line exceeds the max length, also flush it to avoid
    // information loss and start a new line.
    flushBufferedLine();
    // If the character is not a newline, add it to the new line buffer.
    if (c != '\n' && c != '\r') {
      bufferedLine_ += c;
    }
  }
}

void LogOutput::flushBufferedLine() {
  bufferedLine_.trim();
  if (bufferedLine_.isEmpty()) {
    bufferedLine_ = "";
    return;
  }

  if (WiFi.status() != WL_CONNECTED || !ensureSyslogServerResolved()) {
    bufferedLine_ = "";
    return;
  }

  if (syslogUdp.beginPacket(syslogServerIp_, SYSLOG_PORT)) {
    syslogUdp.print("<14>tallyclient: ");
    syslogUdp.print(bufferedLine_);
    syslogUdp.print('\n');
    syslogUdp.endPacket();
  }

  bufferedLine_ = "";
}

bool LogOutput::ensureSyslogServerResolved() {
  if (syslogServerResolved_) {
    return true;
  }

  const unsigned long now = millis();
  if (lastResolveAttemptMs_ != 0 && now - lastResolveAttemptMs_ < DNS_RETRY_INTERVAL_MS) {
    return false;
  }
  lastResolveAttemptMs_ = now;

  IPAddress resolved;
  if (WiFi.hostByName(syslogServerDns_.c_str(), resolved) == 1) {
    syslogServerIp_ = resolved;
    syslogServerResolved_ = true;
    return true;
  }

  return false;
}
