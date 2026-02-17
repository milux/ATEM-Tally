# OTA via `espota` + custom SHA-256 + Ed25519 signatures

This project now uses custom OTA signature verification while keeping PlatformIO `espota` / ArduinoOTA transport.

## Security model

- Firmware transfer: `espota` (ArduinoOTA transport).
- Firmware integrity/authenticity: custom footer with SHA-256 digest + Ed25519 signature.
- Pre-boot decision: immediately after OTA transfer ends, current firmware verifies the staged image in the boot partition before reboot.
- Runtime decision: if a new image still reaches `ESP_OTA_IMG_PENDING_VERIFY`, it is verified again on first boot.
- Rollback safety: failed verification either reverts boot partition before reboot or triggers rollback during pending-verify boot.

IDF signed-app and Secure Boot build signing are disabled.

## 1) Configuration in this repo

- `platformio.ini` uses `scripts/ota_custom_signing.py` as pre-script.
- That script prepares key material/public key header and registers signing actions for both:
	- post-build (`buildprog`)
	- pre-upload (`upload`), ensuring OTA uploads never send an unsigned artifact.
- `sdkconfig.defaults` keeps rollback enabled (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) but disables IDF signed-app / Secure Boot signing flags.

## 2) Signing material

The build script manages key material under `keys/`:

- private key: `keys/ota_ed25519_private.pem`
- public key (PEM): `keys/ota_ed25519_public.pem`
- public key (DER): `keys/ota_ed25519_public.der`

If the private key does not exist, it is generated automatically with OpenSSL (`ED25519`).

The script also updates `src/ota_signing_public_key.h` from the public key.

## 3) OTA image format

After normal firmware build, the script appends a footer to `${PROGNAME}.bin`:

- magic (`TLYSIGV1`)
- original unsigned image length (uint32)
- SHA-256 of unsigned image bytes
- Ed25519 signature over the SHA-256 digest
- end magic (`1VGISYLT`)

The unsigned copy is saved as `${PROGNAME}.bin.unsigned`.

## 4) Build and OTA upload

Serial build:

```bash
platformio run -e az-delivery-devkit-v4
```

OTA upload:

```bash
platformio run -t upload -e az-delivery-devkit-v4-ota
```

## 5) Runtime verification behavior

### Phase A: pre-reboot verification (old/running firmware)

Immediately after OTA transfer completes:

1. Auto reboot is disabled for OTA success.
2. Running firmware verifies signature footer, SHA-256, and Ed25519 signature on the staged boot partition.
3. If verification fails, boot partition is reverted to the current running image and reboot is aborted.
4. If verification succeeds, device reboots into the new image.

### Phase B: pending-verify confirmation (new firmware)

On first boot after OTA:

1. App checks for `ESP_OTA_IMG_PENDING_VERIFY`.
2. App scans the running OTA partition for the signature footer.
3. App computes SHA-256 over the firmware bytes defined by the footer.
4. App compares digest and verifies Ed25519 signature with embedded public key.
5. App confirms image (`esp_ota_mark_app_valid_cancel_rollback`) or rolls back immediately.

## 6) Functional test procedure

### Positive path (valid update)

1. Build signed image: `platformio run -e az-delivery-devkit-v4`
2. OTA upload: `platformio run -t upload -e az-delivery-devkit-v4-ota`
3. Watch serial monitor after reboot.
4. Expected log includes: `OTA: signature verified, image confirmed`.

### Negative path A (tampered payload)

1. Build once to generate `.bin` and `.bin.unsigned`.
2. Copy signed bin and flip one byte in firmware area before footer.
3. Upload tampered image via OTA.
4. Expected behavior: SHA-256 mismatch, rollback, automatic reboot to previous image.

### Negative path B (wrong signing key)

1. Backup `keys/ota_ed25519_private.pem`.
2. Replace it with a different Ed25519 private key.
3. Build + OTA upload.
4. Expected behavior: Ed25519 verification fails and image rolls back.
5. Restore original key afterwards.

## 7) Notes

- This design intentionally does not use IDF signed-app verification or Secure Boot eFuse flow.
- Keep `keys/` private and out of version control.
