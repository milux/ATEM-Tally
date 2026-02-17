#include "config_api.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>

#include "log_output.h"

namespace config_api {

namespace {

constexpr uint8_t DEFAULT_LISTEN_INPUT = 2;
constexpr uint16_t DEFAULT_KEEP_ALIVE_SECONDS = 5;
constexpr uint16_t DEFAULT_RESTART_TIMEOUT_SECONDS = 30;
constexpr const char* DEFAULT_SYSLOG_SERVER_DNS = "syslog.internal";

const char* CONFIG_NAMESPACE = "tallycfg";
const char* CONFIG_KEY_PASSWORD = "api_pwd";
const char* CONFIG_KEY_LISTEN_INPUT = "listen_in";
const char* CONFIG_KEY_KEEP_ALIVE_SECONDS = "keep_sec";
const char* CONFIG_KEY_RESTART_TIMEOUT_SECONDS = "rst_sec";
const char* CONFIG_KEY_SYSLOG_SERVER_DNS = "syslog_dns";

WebServer apiServer(80);
Preferences preferences;
ClientConfig* runtimeConfigPtr = nullptr;
std::function<void()> onListenInputChangedCb;
std::function<void()> onKeepAliveChangedCb;
std::function<void()> onSyslogServerChangedCb;

void sendJsonResponse(int statusCode, const char* status, const char* message) {
  StaticJsonDocument<192> response;
  response["status"] = status;
  response["message"] = message;
  String body;
  serializeJson(response, body);
  apiServer.send(statusCode, "application/json", body);
}

bool parseRequestBody(StaticJsonDocument<192>& body) {
  if (!apiServer.hasArg("plain")) {
    sendJsonResponse(400, "error", "Missing request body");
    return false;
  }

  const DeserializationError error = deserializeJson(body, apiServer.arg("plain"));
  if (error) {
    sendJsonResponse(400, "error", "Invalid JSON body");
    return false;
  }

  return true;
}

bool hasApiAccess(const ClientConfig& runtimeConfig) {
  if (runtimeConfig.apiPassword.isEmpty()) {
    return true;
  }

  if (apiServer.hasHeader("X-Api-Password") && apiServer.header("X-Api-Password") == runtimeConfig.apiPassword) {
    return true;
  }

  if (apiServer.hasArg("password") && apiServer.arg("password") == runtimeConfig.apiPassword) {
    return true;
  }

  return false;
}

bool requireApiAccess(const ClientConfig& runtimeConfig) {
  if (hasApiAccess(runtimeConfig)) {
    return true;
  }

  sendJsonResponse(401, "error", "Unauthorized");
  return false;
}

void loadConfig(ClientConfig& runtimeConfig) {
  runtimeConfig.apiPassword = preferences.getString(CONFIG_KEY_PASSWORD, "");
  runtimeConfig.listenInput = preferences.getUChar(CONFIG_KEY_LISTEN_INPUT, DEFAULT_LISTEN_INPUT);
  runtimeConfig.keepAliveSeconds =
      preferences.getUShort(CONFIG_KEY_KEEP_ALIVE_SECONDS, DEFAULT_KEEP_ALIVE_SECONDS);
  runtimeConfig.restartTimeoutSeconds =
      preferences.getUShort(CONFIG_KEY_RESTART_TIMEOUT_SECONDS, DEFAULT_RESTART_TIMEOUT_SECONDS);
  runtimeConfig.syslogServerDns =
      preferences.getString(CONFIG_KEY_SYSLOG_SERVER_DNS, DEFAULT_SYSLOG_SERVER_DNS);

  if (runtimeConfig.keepAliveSeconds == 0) {
    runtimeConfig.keepAliveSeconds = DEFAULT_KEEP_ALIVE_SECONDS;
    preferences.putUShort(CONFIG_KEY_KEEP_ALIVE_SECONDS, runtimeConfig.keepAliveSeconds);
  }

  if (runtimeConfig.listenInput == 0xffu) {
    runtimeConfig.listenInput = DEFAULT_LISTEN_INPUT;
    preferences.putUChar(CONFIG_KEY_LISTEN_INPUT, runtimeConfig.listenInput);
  }

  if (runtimeConfig.restartTimeoutSeconds == 0) {
    runtimeConfig.restartTimeoutSeconds = DEFAULT_RESTART_TIMEOUT_SECONDS;
    preferences.putUShort(CONFIG_KEY_RESTART_TIMEOUT_SECONDS, runtimeConfig.restartTimeoutSeconds);
  }

  if (runtimeConfig.syslogServerDns.isEmpty()) {
    runtimeConfig.syslogServerDns = DEFAULT_SYSLOG_SERVER_DNS;
    preferences.putString(CONFIG_KEY_SYSLOG_SERVER_DNS, runtimeConfig.syslogServerDns);
  }
}

void handleSetPassword() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  if (!runtimeConfig.apiPassword.isEmpty() && !requireApiAccess(runtimeConfig)) {
    return;
  }

  StaticJsonDocument<192> body;
  if (!parseRequestBody(body)) {
    return;
  }

  if (!body.containsKey("password") || !body["password"].is<const char*>()) {
    sendJsonResponse(400, "error", "Field 'password' must be a string");
    return;
  }

  runtimeConfig.apiPassword = String(body["password"].as<const char*>());
  preferences.putString(CONFIG_KEY_PASSWORD, runtimeConfig.apiPassword);
  sendJsonResponse(200, "ok", "Password updated");
}

void handleSetListenInput() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  if (!requireApiAccess(runtimeConfig)) {
    return;
  }

  StaticJsonDocument<192> body;
  if (!parseRequestBody(body)) {
    return;
  }

  if (!body.containsKey("listen_input") || !body["listen_input"].is<unsigned int>()) {
    sendJsonResponse(400, "error", "Field 'listen_input' must be an integer");
    return;
  }

  const unsigned int listenInput = body["listen_input"].as<unsigned int>();
  if (listenInput > 254) {
    sendJsonResponse(400, "error", "Field 'listen_input' must be between 0 and 254");
    return;
  }

  runtimeConfig.listenInput = static_cast<uint8_t>(listenInput);
  preferences.putUChar(CONFIG_KEY_LISTEN_INPUT, runtimeConfig.listenInput);

  if (onListenInputChangedCb) {
    onListenInputChangedCb();
  }

  sendJsonResponse(200, "ok", "Listening input updated");
}

void handleGetListenInput() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  StaticJsonDocument<192> response;
  response["status"] = "ok";
  response["listen_input"] = runtimeConfig.listenInput;
  String body;
  serializeJson(response, body);
  apiServer.send(200, "application/json", body);
}

void handleSetKeepAliveSeconds() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  if (!requireApiAccess(runtimeConfig)) {
    return;
  }

  StaticJsonDocument<192> body;
  if (!parseRequestBody(body)) {
    return;
  }

  if (!body.containsKey("keep_alive_seconds") || !body["keep_alive_seconds"].is<unsigned int>()) {
    sendJsonResponse(400, "error", "Field 'keep_alive_seconds' must be an integer");
    return;
  }

  const unsigned int keepAliveSeconds = body["keep_alive_seconds"].as<unsigned int>();
  if (keepAliveSeconds == 0 || keepAliveSeconds > 3600) {
    sendJsonResponse(400, "error", "Field 'keep_alive_seconds' must be between 1 and 3600");
    return;
  }

  runtimeConfig.keepAliveSeconds = static_cast<uint16_t>(keepAliveSeconds);
  preferences.putUShort(CONFIG_KEY_KEEP_ALIVE_SECONDS, runtimeConfig.keepAliveSeconds);

  if (onKeepAliveChangedCb) {
    onKeepAliveChangedCb();
  }

  sendJsonResponse(200, "ok", "Keep-alive interval updated");
}

void handleGetKeepAliveSeconds() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  StaticJsonDocument<192> response;
  response["status"] = "ok";
  response["keep_alive_seconds"] = runtimeConfig.keepAliveSeconds;
  String body;
  serializeJson(response, body);
  apiServer.send(200, "application/json", body);
}

void handleSetRestartTimeoutSeconds() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  if (!requireApiAccess(runtimeConfig)) {
    return;
  }

  StaticJsonDocument<192> body;
  if (!parseRequestBody(body)) {
    return;
  }

  if (!body.containsKey("restart_timeout_seconds") || !body["restart_timeout_seconds"].is<unsigned int>()) {
    sendJsonResponse(400, "error", "Field 'restart_timeout_seconds' must be an integer");
    return;
  }

  const unsigned int restartTimeoutSeconds = body["restart_timeout_seconds"].as<unsigned int>();
  if (restartTimeoutSeconds == 0 || restartTimeoutSeconds > 65535) {
    sendJsonResponse(400, "error", "Field 'restart_timeout_seconds' must be between 1 and 65535");
    return;
  }

  runtimeConfig.restartTimeoutSeconds = static_cast<uint16_t>(restartTimeoutSeconds);
  preferences.putUShort(CONFIG_KEY_RESTART_TIMEOUT_SECONDS, runtimeConfig.restartTimeoutSeconds);

  if (onKeepAliveChangedCb) {
    onKeepAliveChangedCb();
  }

  sendJsonResponse(200, "ok", "Restart timeout updated");
}

void handleGetRestartTimeoutSeconds() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  StaticJsonDocument<192> response;
  response["status"] = "ok";
  response["restart_timeout_seconds"] = runtimeConfig.restartTimeoutSeconds;
  String body;
  serializeJson(response, body);
  apiServer.send(200, "application/json", body);
}

void handleSetSyslogServerDns() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  if (!requireApiAccess(runtimeConfig)) {
    return;
  }

  StaticJsonDocument<192> body;
  if (!parseRequestBody(body)) {
    return;
  }

  if (!body.containsKey("syslog_server") || !body["syslog_server"].is<const char*>()) {
    sendJsonResponse(400, "error", "Field 'syslog_server' must be a string");
    return;
  }

  const String syslogServerDns = String(body["syslog_server"].as<const char*>());
  if (syslogServerDns.isEmpty() || syslogServerDns.length() > 253) {
    sendJsonResponse(400, "error", "Field 'syslog_server' must be between 1 and 253 characters");
    return;
  }

  runtimeConfig.syslogServerDns = syslogServerDns;
  preferences.putString(CONFIG_KEY_SYSLOG_SERVER_DNS, runtimeConfig.syslogServerDns);

  if (onSyslogServerChangedCb) {
    onSyslogServerChangedCb();
  }

  sendJsonResponse(200, "ok", "Syslog server updated");
}

void handleGetSyslogServerDns() {
  if (runtimeConfigPtr == nullptr) {
    sendJsonResponse(500, "error", "Configuration API not initialized");
    return;
  }

  ClientConfig& runtimeConfig = *runtimeConfigPtr;

  StaticJsonDocument<192> response;
  response["status"] = "ok";
  response["syslog_server"] = runtimeConfig.syslogServerDns;
  String body;
  serializeJson(response, body);
  apiServer.send(200, "application/json", body);
}

void handleNotFound() {
  sendJsonResponse(404, "error", "Endpoint not found");
}

}  // namespace

void begin(ClientConfig* runtimeConfig,
           std::function<void()> onListenInputChanged,
           std::function<void()> onKeepAliveChanged,
           std::function<void()> onSyslogServerChanged) {
  if (runtimeConfig == nullptr) {
    LogSerial.println("REST API init failed: runtime config pointer is null.");
    return;
  }

  preferences.begin(CONFIG_NAMESPACE, false);
  loadConfig(*runtimeConfig);

  runtimeConfigPtr = runtimeConfig;
  onListenInputChangedCb = onListenInputChanged;
  onKeepAliveChangedCb = onKeepAliveChanged;
  onSyslogServerChangedCb = onSyslogServerChanged;

  const char* headerKeys[] = {"X-Api-Password"};
  apiServer.collectHeaders(headerKeys, 1);

  apiServer.on("/api/password", HTTP_POST, handleSetPassword);
  apiServer.on("/api/listen-input", HTTP_POST, handleSetListenInput);
  apiServer.on("/api/listen-input", HTTP_GET, handleGetListenInput);
  apiServer.on("/api/keep-alive", HTTP_POST, handleSetKeepAliveSeconds);
  apiServer.on("/api/keep-alive", HTTP_GET, handleGetKeepAliveSeconds);
  apiServer.on("/api/restart-timeout", HTTP_POST, handleSetRestartTimeoutSeconds);
  apiServer.on("/api/restart-timeout", HTTP_GET, handleGetRestartTimeoutSeconds);
  apiServer.on("/api/syslog-server", HTTP_POST, handleSetSyslogServerDns);
  apiServer.on("/api/syslog-server", HTTP_GET, handleGetSyslogServerDns);
  apiServer.onNotFound(handleNotFound);

  apiServer.begin();
  LogSerial.println("REST API server started on port 80.");
}

void handleClient() {
  apiServer.handleClient();
}

}  // namespace config_api
