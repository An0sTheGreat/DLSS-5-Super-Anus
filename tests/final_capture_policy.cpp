#include "../src/final_capture_policy.hpp"
#include "../src/screenshot_pixels.hpp"
#include <cassert>
#include <cstdio>
int main()
{
    nr::FinalCaptureTiming p;
    assert(!p.ready(10,1,1));
    p.start(1000,10,20);
    assert(!p.ready(1000,11,20));
    assert(!p.ready(1016,10,20)); // a generated/repeated frame with no bypass
    assert(p.ready(1016,11,20));
    assert(p.ready(1499,11,20));
    assert(!p.ready(1500,11,20));
    assert(!p.ready(1016,11,21)); // interleaved successful NR, reject mixed pair
    assert(p.expired(1500));
    using namespace nr::screenshots;
    const auto a = png_rgb({.18f,.18f,.18f},Encoding::scrgb,true,80.f);
    const auto b = png_rgb({.18f*203.f/80.f,.18f*203.f/80.f,.18f*203.f/80.f},Encoding::scrgb,true,203.f);
    assert(std::fabs(a.r-b.r)<1e-6f);
    const auto c = png_rgb({.18f,.18f,.18f},Encoding::linear709,true,203.f);
    assert(std::fabs(a.r-c.r)<1e-6f); // never apply display nits to scene-linear SR
    const auto s = png_rgb({.4f,.4f,.4f},Encoding::srgb,false,203.f);
    assert(s.r == .4f);
    std::puts("Final capture: <500 ms pair acceptance, fresh bypass, mixed-frame rejection, calibrated scRGB and untouched scene-linear/SDR passed.");
}
