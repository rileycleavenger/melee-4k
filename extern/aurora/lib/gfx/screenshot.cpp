#include "screenshot.hpp"

#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace aurora::gfx::screenshot {
namespace {
constexpr Module Log{"aurora::screenshot"};

bool g_parsed = false;
std::set<uint64_t> g_frames;
std::filesystem::path g_dir;
uint64_t g_frame = 0;

struct Pending {
  wgpu::Buffer buffer;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t rowPitch = 0;
  bool swapRedBlue = false;
  uint64_t frame = 0;
};
Pending g_pending;

void parse_env() {
  g_parsed = true;
  const char* frames = std::getenv("AURORA_SCREENSHOT_FRAMES");
  if (frames == nullptr || frames[0] == '\0') {
    return;
  }
  for (const char* p = frames; *p != '\0';) {
    char* end = nullptr;
    const auto n = std::strtoull(p, &end, 10);
    if (end == p) {
      ++p;
      continue;
    }
    g_frames.insert(n);
    p = end;
  }
  if (const char* dir = std::getenv("AURORA_SCREENSHOT_DIR"); dir != nullptr && dir[0] != '\0') {
    g_dir = dir;
  } else {
    g_dir = std::filesystem::path(g_config.cachePath != nullptr ? g_config.cachePath : ".") / "screenshots";
  }
  std::error_code ec;
  std::filesystem::create_directories(g_dir, ec);
  Log.info("Capturing {} frame(s) to {}", g_frames.size(), g_dir.string());
}

bool write_tga(const std::filesystem::path& path, const Pending& p, const uint8_t* pixels) {
  FILE* f = std::fopen(path.string().c_str(), "wb");
  if (f == nullptr) {
    return false;
  }
  uint8_t header[18] = {};
  header[2] = 2; // uncompressed true-colour
  header[12] = p.width & 0xFF;
  header[13] = p.width >> 8;
  header[14] = p.height & 0xFF;
  header[15] = p.height >> 8;
  header[16] = 32;
  header[17] = 0x28; // 8 alpha bits, top-left origin
  bool ok = std::fwrite(header, sizeof header, 1, f) == 1;
  std::vector<uint8_t> row(p.width * 4);
  for (uint32_t y = 0; ok && y < p.height; ++y) {
    std::memcpy(row.data(), pixels + static_cast<size_t>(y) * p.rowPitch, row.size());
    for (uint32_t x = 0; x < p.width; ++x) {
      uint8_t* px = &row[x * 4];
      if (p.swapRedBlue) {
        std::swap(px[0], px[2]);
      }
      px[3] = 0xFF; // the EFB's alpha is not coverage; an opaque image is what was shown
    }
    ok = std::fwrite(row.data(), row.size(), 1, f) == 1;
  }
  return std::fclose(f) == 0 && ok;
}
} // namespace

bool begin_frame() noexcept {
  if (!g_parsed) {
    parse_env();
  }
  ++g_frame;
  return !g_frames.empty() && g_frames.contains(g_frame);
}

void record(const wgpu::CommandEncoder& encoder, const webgpu::TextureWithSampler& source) noexcept {
  const bool bgra = source.format == wgpu::TextureFormat::BGRA8Unorm ||
                    source.format == wgpu::TextureFormat::BGRA8UnormSrgb;
  const bool rgba = source.format == wgpu::TextureFormat::RGBA8Unorm ||
                    source.format == wgpu::TextureFormat::RGBA8UnormSrgb;
  if (!bgra && !rgba) {
    Log.warn("Frame {}: unsupported framebuffer format {}, not captured", g_frame,
             static_cast<uint32_t>(source.format));
    return;
  }
  Pending p;
  p.width = source.size.width;
  p.height = source.size.height;
  p.rowPitch = (p.width * 4 + 255) & ~255u;
  p.swapRedBlue = rgba;
  p.frame = g_frame;
  const wgpu::BufferDescriptor desc{
      .label = "Screenshot readback",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = static_cast<uint64_t>(p.rowPitch) * p.height,
  };
  p.buffer = webgpu::g_device.CreateBuffer(&desc);
  const wgpu::TexelCopyTextureInfo src{.texture = source.texture};
  const wgpu::TexelCopyBufferInfo dst{
      .layout = {.bytesPerRow = p.rowPitch, .rowsPerImage = p.height},
      .buffer = p.buffer,
  };
  const wgpu::Extent3D extent{p.width, p.height, 1};
  encoder.CopyTextureToBuffer(&src, &dst, &extent);
  g_pending = std::move(p);
}

void after_submit() noexcept {
  if (!g_pending.buffer) {
    return;
  }
  Pending p = std::move(g_pending);
  g_pending = {};
  const uint64_t size = static_cast<uint64_t>(p.rowPitch) * p.height;
  bool mapped = false;
  const auto future = p.buffer.MapAsync(wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
                                        [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                          mapped = status == wgpu::MapAsyncStatus::Success;
                                        });
  webgpu::g_instance.WaitAny(future, UINT64_MAX);
  if (!mapped) {
    Log.warn("Frame {}: readback failed", p.frame);
    return;
  }
  const auto* pixels = static_cast<const uint8_t*>(p.buffer.GetConstMappedRange(0, size));
  const auto path = g_dir / ("frame_" + std::to_string(p.frame) + ".tga");
  if (write_tga(path, p, pixels)) {
    Log.info("Frame {}: wrote {}x{} {}", p.frame, p.width, p.height, path.string());
  } else {
    Log.warn("Frame {}: could not write {}", p.frame, path.string());
  }
  p.buffer.Unmap();
}
} // namespace aurora::gfx::screenshot
