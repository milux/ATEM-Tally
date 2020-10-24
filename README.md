# ATEM-Tally
DIY ATEM Tally using nodejs Server and ESP32 Dev Board Clients

This project consists of two parts:
- Server running on Node.js, with connects to the ATEM server and receives changes
- One or more clients running native code on ESP32 Dev Kits, which pull updates for one given ATEM channel from the server via WiFi and toggle 2 LEDs (or whatever you build into your tally) for signaling of Preview and Program states

## Requirements/Building
We recommend **VS Code with PlatformIO** for code changes and ESP32 flashing.

Server is built by switching into the `TallyServer` directory and executing `yarn run tsc`. (Which requires Node.js and yarn to be installed, obviously.)
