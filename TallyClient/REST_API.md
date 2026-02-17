# Tally Client REST API

This document describes the REST API exposed by the Tally Client firmware.

## Base URL

Use the device IP address on port `80`:

- `http://<tally-client-ip>`

Example:

- `http://192.168.44.172`

## Authentication

The API uses a configured password.

- Send password in header: `X-Api-Password: <password>`
- Or as query parameter: `?password=<password>`

Rules:

- If no password is set yet, `POST /api/password` is allowed without authentication.
- `GET /api/listen-input`, `GET /api/keep-alive`, `GET /api/restart-timeout`, and `GET /api/syslog-server` are public (no authentication required).
- After password is set, all other documented endpoints require authentication.

## Endpoints

### 1) Set API password

- **Method:** `POST`
- **Path:** `/api/password`
- **Body:**

```json
{
  "password": "my-secret"
}
```

- **Success response (200):**

```json
{
  "status": "ok",
  "message": "Password updated"
}
```

Example:

```bash
curl -X POST http://192.168.44.172/api/password \
  -H "Content-Type: application/json" \
  -d '{"password":"my-secret"}'
```

---

### 2) Set listening input

- **Method:** `POST`
- **Path:** `/api/listen-input`
- **Auth:** required (unless initial password is still unset)
- **Body:**

```json
{
  "listen_input": 2
}
```

- **Constraints:** `listen_input` must be an integer in range `0..254`.
- **Success response (200):**

```json
{
  "status": "ok",
  "message": "Listening input updated"
}
```

Example:

```bash
curl -X POST http://192.168.44.172/api/listen-input \
  -H "Content-Type: application/json" \
  -H "X-Api-Password: my-secret" \
  -d '{"listen_input":4}'
```

---

### 3) Get listening input

- **Method:** `GET`
- **Path:** `/api/listen-input`
- **Auth:** not required
- **Success response (200):**

```json
{
  "status": "ok",
  "listen_input": 4
}
```

Example:

```bash
curl -X GET http://192.168.44.172/api/listen-input
```

---

### 4) Set keep-alive interval

- **Method:** `POST`
- **Path:** `/api/keep-alive`
- **Auth:** required (unless initial password is still unset)
- **Body:**

```json
{
  "keep_alive_seconds": 5
}
```

- **Constraints:** `keep_alive_seconds` must be an integer in range `1..3600`.
- **Success response (200):**

```json
{
  "status": "ok",
  "message": "Keep-alive interval updated"
}
```

Example:

```bash
curl -X POST http://192.168.44.172/api/keep-alive \
  -H "Content-Type: application/json" \
  -H "X-Api-Password: my-secret" \
  -d '{"keep_alive_seconds":10}'
```

---

### 5) Get keep-alive interval

- **Method:** `GET`
- **Path:** `/api/keep-alive`
- **Auth:** not required
- **Success response (200):**

```json
{
  "status": "ok",
  "keep_alive_seconds": 10
}
```

Example:

```bash
curl -X GET http://192.168.44.172/api/keep-alive
```

---

### 6) Set restart timeout

- **Method:** `POST`
- **Path:** `/api/restart-timeout`
- **Auth:** required (unless initial password is still unset)
- **Body:**

```json
{
  "restart_timeout_seconds": 30
}
```

- **Constraints:** `restart_timeout_seconds` must be an integer in range `1..65535`.
- **Success response (200):**

```json
{
  "status": "ok",
  "message": "Restart timeout updated"
}
```

Example:

```bash
curl -X POST http://192.168.44.172/api/restart-timeout \
  -H "Content-Type: application/json" \
  -H "X-Api-Password: my-secret" \
  -d '{"restart_timeout_seconds":30}'
```

---

### 7) Get restart timeout

- **Method:** `GET`
- **Path:** `/api/restart-timeout`
- **Auth:** not required
- **Success response (200):**

```json
{
  "status": "ok",
  "restart_timeout_seconds": 30
}
```

Example:

```bash
curl -X GET http://192.168.44.172/api/restart-timeout
```

---

### 8) Set syslog server DNS

- **Method:** `POST`
- **Path:** `/api/syslog-server`
- **Auth:** required (unless initial password is still unset)
- **Body:**

```json
{
  "syslog_server": "syslog.internal"
}
```

- **Constraints:** `syslog_server` must be a string with length `1..253`.
- **Default:** `syslog.internal`
- **Success response (200):**

```json
{
  "status": "ok",
  "message": "Syslog server updated"
}
```

Example:

```bash
curl -X POST http://192.168.44.172/api/syslog-server \
  -H "Content-Type: application/json" \
  -H "X-Api-Password: my-secret" \
  -d '{"syslog_server":"syslog.internal"}'
```

---

### 9) Get syslog server DNS

- **Method:** `GET`
- **Path:** `/api/syslog-server`
- **Auth:** not required
- **Success response (200):**

```json
{
  "status": "ok",
  "syslog_server": "syslog.internal"
}
```

Example:

```bash
curl -X GET http://192.168.44.172/api/syslog-server
```

## Common error responses

- **401 Unauthorized**

```json
{
  "status": "error",
  "message": "Unauthorized"
}
```

- **400 Bad Request** (example)

```json
{
  "status": "error",
  "message": "Field 'keep_alive_seconds' must be an integer"
}
```

- **404 Not Found**

```json
{
  "status": "error",
  "message": "Endpoint not found"
}
```
