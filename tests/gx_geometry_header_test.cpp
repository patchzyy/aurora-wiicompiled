#include <dolphin/gx.h>

#include <gtest/gtest.h>

namespace {
struct ArrayCall {
  GXAttr attr{};
  const void* data = nullptr;
  u32 size = 0;
  u8 stride = 0;
  bool le = true;
} s_call;
} // namespace

extern "C" void GXSetArray(GXAttr attr, const void* data, u32 size, u8 stride, bool le) {
  s_call = {attr, data, size, stride, le};
}

TEST(GxGeometryHeaderTest, DefaultArrayUsesBigEndian) {
  const u32 data[] = {1, 2};
  GXSetArray(GX_VA_POS, data, sizeof(data), sizeof(u32));
  EXPECT_EQ(s_call.attr, GX_VA_POS);
  EXPECT_EQ(s_call.data, data);
  EXPECT_EQ(s_call.size, sizeof(data));
  EXPECT_EQ(s_call.stride, sizeof(u32));
  EXPECT_FALSE(s_call.le);
}

TEST(GxGeometryHeaderTest, ExplicitEndianIsPreserved) {
  const u32 data[] = {3, 4};
  GXSETARRAY(GX_VA_NRM, data, sizeof(data), sizeof(u32), true);
  EXPECT_EQ(s_call.attr, GX_VA_NRM);
  EXPECT_EQ(s_call.data, data);
  EXPECT_EQ(s_call.size, sizeof(data));
  EXPECT_EQ(s_call.stride, sizeof(u32));
  EXPECT_TRUE(s_call.le);
}
