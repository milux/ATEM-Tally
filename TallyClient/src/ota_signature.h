#pragma once

#include <cstddef>

namespace ota_signature {

bool verifyStagedOtaImage(size_t stagedImageLength);
bool confirmPendingOtaImage();

}  // namespace ota_signature
