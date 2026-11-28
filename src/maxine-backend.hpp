#pragma once

#include <cstdint>

struct gs_texture;

namespace maxine {

// Loads the runtime on first call. Returns false when it is not installed.
bool available();

// Upscales src into an internal RGBA8 texture and returns it, or null on
// failure. The returned texture is owned by this module.
struct gs_texture *upscale(struct gs_texture *src, uint32_t in_width,
                           uint32_t in_height, uint32_t out_width,
                           uint32_t out_height);

void shutdown();

} // namespace maxine
