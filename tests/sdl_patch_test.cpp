#include <SDL3/SDL_stdinc.h>

#include <gtest/gtest.h>

// SDL internal, only reachable when the vendored SDL is linked statically.
extern "C" bool SDL_IsJoystickNintendoSwitchProInputOnly(Uint16 vendor_id, Uint16 product_id);

namespace {

TEST(SdlPatchTest, PdpAfterglowWaveUsesInputOnlyProtocol) {
  EXPECT_TRUE(SDL_IsJoystickNintendoSwitchProInputOnly(0x0e6f, 0x018b));
}

TEST(SdlPatchTest, PdpAfterglowWirelessKeepsFullProtocol) {
  EXPECT_FALSE(SDL_IsJoystickNintendoSwitchProInputOnly(0x0e6f, 0x0186));
}

} // namespace
