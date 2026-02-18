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

void LogOutput::flush() {
  flushQueuedLines();
}

void LogOutput::setSyslogServer(const String& serverDns) {
  if (serverDns == syslogServerDns_) {
    return;
  }

  syslogServerDns_ = serverDns;
  syslogServerResolved_ = false;
  lastResolveAttemptMs_ = 0;
}

void LogOutput::setListenInput(uint8_t listenInput) {
  listenInput_ = listenInput;
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
  if (c == '\n' || c == '\r') {
    flushBufferedLine();
    return;
  }

  if (bufferedLine_.length() < MAX_BUFFERED_LOG_LINE) {
    bufferedLine_ += c;
    return;
  }

  // If the line exceeds the max length, flush it and continue with a new line.
  flushBufferedLine();
  bufferedLine_ += c;
}

void LogOutput::enqueueLine(const String& line) {
  if (line.isEmpty()) {
    return;
  }

  const size_t tail = (queuedHead_ + queuedCount_) % MAX_BUFFERED_LOG_LINES;
  queuedLines_[tail] = line;

  if (queuedCount_ < MAX_BUFFERED_LOG_LINES) {
    ++queuedCount_;
    return;
  }

  // Queue full: drop the oldest line to keep most recent messages.
  queuedHead_ = (queuedHead_ + 1) % MAX_BUFFERED_LOG_LINES;
}

bool LogOutput::sendLine(const String& line) {
  if (!syslogUdp.beginPacket(syslogServerIp_, SYSLOG_PORT)) {
    return false;
  }

  syslogUdp.print("<14>tallyclient-");
  syslogUdp.print(listenInput_);
  syslogUdp.print(": ");
  syslogUdp.print(line);
  syslogUdp.print('\n');

  return syslogUdp.endPacket() == 1;
}

void LogOutput::flushQueuedLines() {
  // Only attempt to send if connected to WiFi, a listen input is set,
  // and the syslog server is resolved.
  if (WiFi.status() != WL_CONNECTED || listenInput_ == 255 || !ensureSyslogServerResolved()) {
    return;
  }

  while (queuedCount_ > 0) {
    const String& line = queuedLines_[queuedHead_];
    if (!sendLine(line)) {
      return;
    }

    queuedLines_[queuedHead_] = "";
    queuedHead_ = (queuedHead_ + 1) % MAX_BUFFERED_LOG_LINES;
    --queuedCount_;
  }
}

void LogOutput::flushBufferedLine() {
  bufferedLine_.trim();
  if (!bufferedLine_.isEmpty()) {
    enqueueLine(bufferedLine_);
  }

  bufferedLine_ = "";
  flushQueuedLines();
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
