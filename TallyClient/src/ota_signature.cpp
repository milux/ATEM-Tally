#include "ota_signature.h"

#include <Arduino.h>
#include <Ed25519.h>
#include <mbedtls/sha256.h>
#include <string.h>

extern "C" {
#include "esp_ota_ops.h"
#include "esp_partition.h"
}

#include "ota_signing_public_key.h"

namespace ota_signature {

namespace {

constexpr uint8_t kMagic[8] = {'T', 'L', 'Y', 'S', 'I', 'G', 'V', '1'};
constexpr uint8_t kEndMagic[8] = {'1', 'V', 'G', 'I', 'S', 'Y', 'L', 'T'};
constexpr size_t kDigestSize = 32;
constexpr size_t kSignatureSize = 64;
constexpr size_t kVerifyChunkSize = 4096;
constexpr size_t kScanChunkSize = 2048;

struct OtaSignatureFooter {
  uint8_t magic[8];
  uint32_t imageLength;
  uint8_t sha256[kDigestSize];
  uint8_t signature[kSignatureSize];
  uint8_t endMagic[8];
} __attribute__((packed));

struct FooterScanStats {
  size_t offsetsScanned = 0;
  size_t magicHits = 0;
  size_t candidateReadFailures = 0;
  size_t rejectedBadMagic = 0;
  size_t rejectedBadEndMagic = 0;
  size_t rejectedImageLength = 0;
  size_t rejectedDigestMismatch = 0;
  size_t accepted = 0;
};

bool calculateSha256(
    const esp_partition_t* partition,
    size_t length,
    uint8_t outDigest[kDigestSize]);

bool readPartition(const esp_partition_t* partition, size_t offset, void* destination, size_t length) {
  if (partition == nullptr || destination == nullptr) {
    return false;
  }

  return esp_partition_read(partition, offset, destination, length) == ESP_OK;
}

bool findSignatureFooter(
    const esp_partition_t* partition,
    OtaSignatureFooter* footer,
    size_t* footerOffsetOut,
    FooterScanStats* statsOut) {
  if (partition == nullptr || footer == nullptr || footerOffsetOut == nullptr) {
    return false;
  }

  FooterScanStats stats;

  const size_t footerSize = sizeof(OtaSignatureFooter);
  if (partition->size < footerSize) {
    if (statsOut != nullptr) {
      *statsOut = stats;
    }
    return false;
  }

  uint8_t scanBuffer[kScanChunkSize + sizeof(kMagic)];
  size_t bestOffset = 0;
  bool found = false;

  for (size_t offset = 0; offset + footerSize <= partition->size; offset += kScanChunkSize) {
    ++stats.offsetsScanned;

    size_t readLength = kScanChunkSize;
    if (offset + readLength > partition->size) {
      readLength = partition->size - offset;
    }

    size_t extendedLength = readLength;
    if (offset + readLength + sizeof(kMagic) <= partition->size) {
      extendedLength += sizeof(kMagic);
    }

    if (!readPartition(partition, offset, scanBuffer, extendedLength)) {
      return false;
    }

    for (size_t i = 0; i + sizeof(kMagic) <= extendedLength; ++i) {
      if (memcmp(scanBuffer + i, kMagic, sizeof(kMagic)) != 0) {
        continue;
      }

      ++stats.magicHits;

      size_t candidateOffset = offset + i;
      if (candidateOffset + footerSize > partition->size) {
        ++stats.rejectedImageLength;
        continue;
      }

      OtaSignatureFooter candidate;
      if (!readPartition(partition, candidateOffset, &candidate, footerSize)) {
        ++stats.candidateReadFailures;
        continue;
      }

      if (memcmp(candidate.magic, kMagic, sizeof(kMagic)) != 0) {
        ++stats.rejectedBadMagic;
        continue;
      }
      if (memcmp(candidate.endMagic, kEndMagic, sizeof(kEndMagic)) != 0) {
        ++stats.rejectedBadEndMagic;
        continue;
      }
      if (candidate.imageLength != candidateOffset) {
        ++stats.rejectedImageLength;
        continue;
      }

      uint8_t digest[kDigestSize];
      if (!calculateSha256(partition, candidate.imageLength, digest)) {
        ++stats.rejectedDigestMismatch;
        continue;
      }
      if (memcmp(digest, candidate.sha256, sizeof(digest)) != 0) {
        ++stats.rejectedDigestMismatch;
        continue;
      }

      if (!found || candidateOffset > bestOffset) {
        bestOffset = candidateOffset;
        *footer = candidate;
        found = true;
      }

      ++stats.accepted;
    }
  }

  if (statsOut != nullptr) {
    *statsOut = stats;
  }

  if (!found) {
    return false;
  }

  *footerOffsetOut = bestOffset;
  return true;
}

bool readFooterAtOffset(
    const esp_partition_t* partition,
    size_t footerOffset,
    OtaSignatureFooter* footer,
    const char* context) {
  if (partition == nullptr || footer == nullptr) {
    return false;
  }

  if (footerOffset + sizeof(OtaSignatureFooter) > partition->size) {
    Serial.printf(
        "OTA: %s direct footer offset out of range: offset=0x%08x footer=0x%08x partition=0x%08x\n",
        context,
        static_cast<unsigned>(footerOffset),
        static_cast<unsigned>(sizeof(OtaSignatureFooter)),
        static_cast<unsigned>(partition->size));
    return false;
  }

  if (!readPartition(partition, footerOffset, footer, sizeof(OtaSignatureFooter))) {
    Serial.printf("OTA: %s direct footer read failed at offset=0x%08x\n", context, static_cast<unsigned>(footerOffset));
    return false;
  }

  if (memcmp(footer->magic, kMagic, sizeof(kMagic)) != 0) {
    Serial.printf("OTA: %s direct footer magic mismatch at offset=0x%08x\n", context, static_cast<unsigned>(footerOffset));
    return false;
  }

  if (memcmp(footer->endMagic, kEndMagic, sizeof(kEndMagic)) != 0) {
    Serial.printf("OTA: %s direct footer end-magic mismatch at offset=0x%08x\n", context, static_cast<unsigned>(footerOffset));
    return false;
  }

  if (footer->imageLength != footerOffset) {
    Serial.printf(
        "OTA: %s direct footer imageLength mismatch: imageLength=0x%08x expected=0x%08x\n",
        context,
        static_cast<unsigned>(footer->imageLength),
        static_cast<unsigned>(footerOffset));
    return false;
  }

  return true;
}

bool calculateSha256(
    const esp_partition_t* partition,
    size_t length,
    uint8_t outDigest[kDigestSize]) {
  if (partition == nullptr || outDigest == nullptr || length == 0 || length > partition->size) {
    return false;
  }

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  if (mbedtls_sha256_starts_ret(&context, 0) != 0) {
    mbedtls_sha256_free(&context);
    return false;
  }

  uint8_t chunk[kVerifyChunkSize];
  size_t offset = 0;
  while (offset < length) {
    size_t chunkLength = length - offset;
    if (chunkLength > sizeof(chunk)) {
      chunkLength = sizeof(chunk);
    }

    if (!readPartition(partition, offset, chunk, chunkLength)) {
      mbedtls_sha256_free(&context);
      return false;
    }

    if (mbedtls_sha256_update_ret(&context, chunk, chunkLength) != 0) {
      mbedtls_sha256_free(&context);
      return false;
    }

    offset += chunkLength;
  }

  bool result = mbedtls_sha256_finish_ret(&context, outDigest) == 0;
  mbedtls_sha256_free(&context);
  return result;
}

bool rollbackAndReboot() {
  Serial.println("OTA signature verification failed. Rolling back...");
  return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
}

bool verifyPartitionSignature(
  const esp_partition_t* partition,
  const char* context,
  size_t expectedSignedLength) {
  if (partition == nullptr) {
    Serial.printf("OTA: %s partition unavailable\n", context);
    return false;
  }

  Serial.printf(
      "OTA: %s partition details: label=%s subtype=%d address=0x%08x size=0x%08x\n",
      context,
      partition->label,
      static_cast<int>(partition->subtype),
      static_cast<unsigned>(partition->address),
      static_cast<unsigned>(partition->size));

  OtaSignatureFooter footer;
  size_t footerOffset = 0;
  bool footerFound = false;

  if (expectedSignedLength > 0) {
    Serial.printf(
        "OTA: %s expected signed length from transport: %u bytes\n",
        context,
        static_cast<unsigned>(expectedSignedLength));

    if (expectedSignedLength < sizeof(OtaSignatureFooter)) {
      Serial.printf(
          "OTA: %s expected length too small for footer (%u < %u)\n",
          context,
          static_cast<unsigned>(expectedSignedLength),
          static_cast<unsigned>(sizeof(OtaSignatureFooter)));
    } else {
      footerOffset = expectedSignedLength - sizeof(OtaSignatureFooter);
      footerFound = readFooterAtOffset(partition, footerOffset, &footer, context);
    }
  }

  if (!footerFound) {
    FooterScanStats stats;
    if (!findSignatureFooter(partition, &footer, &footerOffset, &stats)) {
      Serial.printf("OTA: %s signature footer not found\n", context);
      Serial.printf(
          "OTA: %s scan stats offsets=%u magic_hits=%u read_fail=%u bad_magic=%u bad_end=%u bad_len=%u bad_digest=%u accepted=%u\n",
          context,
          static_cast<unsigned>(stats.offsetsScanned),
          static_cast<unsigned>(stats.magicHits),
          static_cast<unsigned>(stats.candidateReadFailures),
          static_cast<unsigned>(stats.rejectedBadMagic),
          static_cast<unsigned>(stats.rejectedBadEndMagic),
          static_cast<unsigned>(stats.rejectedImageLength),
          static_cast<unsigned>(stats.rejectedDigestMismatch),
          static_cast<unsigned>(stats.accepted));
      return false;
    }

    Serial.printf("OTA: %s footer found by scan fallback at offset=0x%08x\n", context, static_cast<unsigned>(footerOffset));
  }

  uint8_t digest[kDigestSize];
  if (!calculateSha256(partition, footer.imageLength, digest)) {
    Serial.printf("OTA: %s failed to calculate SHA-256\n", context);
    return false;
  }

  if (memcmp(digest, footer.sha256, sizeof(digest)) != 0) {
    Serial.printf("OTA: %s SHA-256 mismatch\n", context);
    return false;
  }

  if (!Ed25519::verify(footer.signature, ota_signing::kPublicKey, footer.sha256, sizeof(footer.sha256))) {
    Serial.printf("OTA: %s Ed25519 signature verification failed\n", context);
    return false;
  }

  Serial.printf(
      "OTA: %s signature verified (partition=%s footer=0x%08x)\n",
      context,
      partition->label,
      static_cast<unsigned>(footerOffset));
  return true;
}

}  // namespace

bool verifyStagedOtaImage(size_t stagedImageLength) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  if (running == nullptr || boot == nullptr) {
    Serial.println("OTA: unable to inspect running/boot partition for staged verification");
    return false;
  }

  if (boot == running) {
    Serial.println("OTA: no staged image found for pre-reboot verification");
    return false;
  }

  Serial.printf("OTA: pre-reboot verification of staged image in partition '%s'\n", boot->label);
  if (verifyPartitionSignature(boot, "staged image", stagedImageLength)) {
    return true;
  }

  if (esp_ota_set_boot_partition(running) == ESP_OK) {
    Serial.printf("OTA: reverted boot partition to current image '%s'\n", running->label);
  } else {
    Serial.println("OTA: failed to revert boot partition after staged verification failure");
  }

  return false;
}

bool confirmPendingOtaImage() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    Serial.println("OTA: running partition unavailable");
    return false;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    Serial.println("OTA: unable to read image state");
    return false;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY) {
    return true;
  }

  Serial.println("OTA: pending verify, validating SHA-256 + Ed25519 signature...");

  if (!verifyPartitionSignature(running, "pending image", 0)) {
    rollbackAndReboot();
    return false;
  }

  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    Serial.println("OTA: failed to mark app valid");
    return false;
  }

  Serial.println("OTA: pending image signature verified, image confirmed");
  return true;
}

}  // namespace ota_signature
