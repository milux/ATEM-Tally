import hashlib
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Any

env: Any = None
Import("env")  # type: ignore[name-defined]

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
KEYS_DIR = PROJECT_DIR / "keys"
PRIVATE_KEY = KEYS_DIR / "ota_ed25519_private.pem"
PUBLIC_KEY_PEM = KEYS_DIR / "ota_ed25519_public.pem"
PUBLIC_KEY_DER = KEYS_DIR / "ota_ed25519_public.der"
PUBLIC_KEY_HEADER = PROJECT_DIR / "src" / "ota_signing_public_key.h"
FIRMWARE_PATH = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))

MAGIC = b"TLYSIGV1"
END_MAGIC = b"1VGISYLT"
FOOTER_FORMAT = "<8sI32s64s8s"
FOOTER_SIZE = struct.calcsize(FOOTER_FORMAT)


def run_checked(args: list[str], cwd: Path | None = None) -> str:
    result = subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({' '.join(args)}):\n{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def ensure_signing_material() -> bytes:
    KEYS_DIR.mkdir(parents=True, exist_ok=True)

    run_checked(["openssl", "version"])

    if not PRIVATE_KEY.exists():
        print(f"Generating Ed25519 OTA private key at {PRIVATE_KEY}")
        run_checked([
            "openssl",
            "genpkey",
            "-algorithm",
            "ED25519",
            "-out",
            str(PRIVATE_KEY),
        ])

    run_checked([
        "openssl",
        "pkey",
        "-in",
        str(PRIVATE_KEY),
        "-pubout",
        "-out",
        str(PUBLIC_KEY_PEM),
    ])

    run_checked([
        "openssl",
        "pkey",
        "-pubin",
        "-in",
        str(PUBLIC_KEY_PEM),
        "-outform",
        "DER",
        "-out",
        str(PUBLIC_KEY_DER),
    ])

    der_bytes = PUBLIC_KEY_DER.read_bytes()
    der_prefix = bytes.fromhex("302a300506032b6570032100")
    if not der_bytes.startswith(der_prefix) or len(der_bytes) != len(der_prefix) + 32:
        raise RuntimeError("Unexpected Ed25519 public key DER format")

    return der_bytes[-32:]


def write_public_key_header(raw_public_key: bytes) -> None:
    key_values = ", ".join(f"0x{byte:02x}" for byte in raw_public_key)
    header = (
        "#pragma once\n\n"
        "#include <stdint.h>\n\n"
        "namespace ota_signing {\n"
        "static constexpr uint8_t kPublicKey[32] = {"
        f"{key_values}"
        "};\n"
        "}  // namespace ota_signing\n"
    )

    existing = PUBLIC_KEY_HEADER.read_text(encoding="utf-8") if PUBLIC_KEY_HEADER.exists() else ""
    if existing != header:
        PUBLIC_KEY_HEADER.write_text(header, encoding="utf-8")
        print(f"Updated OTA public key header at {PUBLIC_KEY_HEADER}")


def already_signed(firmware: bytes) -> bool:
    if len(firmware) < FOOTER_SIZE:
        return False

    footer = firmware[-FOOTER_SIZE:]
    return footer[:8] == MAGIC and footer[-8:] == END_MAGIC


def sign_firmware(target: Any, source: Any, env: Any) -> None:
    firmware_path = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    if not firmware_path.exists():
        raise RuntimeError(f"Firmware image not found for signing: {firmware_path}")

    firmware = firmware_path.read_bytes()
    if already_signed(firmware):
        print(f"Skipping OTA signing: already signed ({firmware_path})")
        return

    digest = hashlib.sha256(firmware).digest()

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        digest_path = temp_path / "digest.bin"
        signature_path = temp_path / "signature.bin"

        digest_path.write_bytes(digest)

        run_checked([
            "openssl",
            "pkeyutl",
            "-sign",
            "-rawin",
            "-inkey",
            str(PRIVATE_KEY),
            "-in",
            str(digest_path),
            "-out",
            str(signature_path),
        ])

        signature = signature_path.read_bytes()

        run_checked([
            "openssl",
            "pkeyutl",
            "-verify",
            "-rawin",
            "-pubin",
            "-inkey",
            str(PUBLIC_KEY_PEM),
            "-sigfile",
            str(signature_path),
            "-in",
            str(digest_path),
        ])

    if len(signature) != 64:
        raise RuntimeError(f"Invalid Ed25519 signature length: {len(signature)}")

    unsigned_path = firmware_path.with_suffix(firmware_path.suffix + ".unsigned")
    shutil.copyfile(firmware_path, unsigned_path)

    footer = struct.pack(
        FOOTER_FORMAT,
        MAGIC,
        len(firmware),
        digest,
        signature,
        END_MAGIC,
    )

    firmware_path.write_bytes(firmware + footer)
    print(
        f"Signed OTA image: {firmware_path} (unsigned copy: {unsigned_path}, footer={FOOTER_SIZE} bytes)"
    )


def configure_signing() -> None:
    public_key = ensure_signing_material()
    write_public_key_header(public_key)
    print(f"OTA signing material ready for {env.subst('$PIOENV')}")
    env.AddPostAction("buildprog", sign_firmware)
    env.AddPreAction("upload", sign_firmware)


configure_signing()
