// The loading screen shown while the data is installed and loaded (see loading_screen.cpp).
#pragma once
#include <string>

#include "render/assets.h"
#include "render/gfx.h"

namespace dd::app {

class LoadingScreen {
public:
    LoadingScreen(gfx::Renderer& r, const std::string& assetsDir);
    ~LoadingScreen();
    // fraction < 0: no known progress (a block sweeps along the bar)
    void Draw(const std::string& msg, float fraction, bool force = false);

private:
    gfx::Renderer& r_;
    gfx::Texture bg_;
    gfx::Font font_;
    double start_ = 0, last_ = -1;
};

} // namespace dd::app
