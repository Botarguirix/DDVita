// What the port's renderer needs (data/render), as tools/export_render.py writes it: textures as
// .tex (RGBA8, bottom row first), sprites.json, the three arenas, trial_swaps.json,
// player_look.json and animations.json.
#pragma once
#include <functional>
#include <string>

#include "install/extract.h"

namespace dd::install {

// progress(done, total) is called while the textures are written
bool ExportRender(const GameFiles& g, const Extracted& ex, const std::string& outDir, std::string* err,
                  const std::function<void(int, int)>& progress = {});

// Texture2D pixels (first mip level, as stored) -> RGBA8 in the same row order; false if the
// format is not supported
bool DecodeTexture(int format, const uint8_t* data, size_t size, int width, int height, std::vector<uint8_t>* rgba);

// Python's round(x, digits)
double RoundTo(double x, int digits);

} // namespace dd::install
