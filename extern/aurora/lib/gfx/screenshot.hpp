#pragma once

#include <webgpu/webgpu_cpp.h>

namespace aurora::webgpu {
struct TextureWithSampler;
}

// Debug capture of the presented frame at full render resolution.
//
//   AURORA_SCREENSHOT_FRAMES=600,1200   frames (counted from the first present) to capture
//   AURORA_SCREENSHOT_DIR=<dir>         output directory (default <cachePath>/screenshots)
//
// Each capture is written as frame_<n>.tga (32-bit BGRA, top-left origin). Capturing
// waits for the GPU, so the frame it lands on is slow; never enable it while timing.
namespace aurora::gfx::screenshot {
// Called once per presented frame, on the thread that records the present.
// Returns true if this frame is captured and record() must be called.
bool begin_frame() noexcept;
// Records a copy of `source` into a readback buffer on `encoder`.
void record(const wgpu::CommandEncoder& encoder, const webgpu::TextureWithSampler& source) noexcept;
// After the encoder's command buffer was submitted: waits for the copy and writes the file.
void after_submit() noexcept;
} // namespace aurora::gfx::screenshot
