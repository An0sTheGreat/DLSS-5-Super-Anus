#define NOMINMAX
#include "../src/screenshot_files.hpp"
#include <cassert>
#include <limits>
int wmain(int argc, wchar_t **argv)
{
    assert(argc == 2);
    using namespace nr::screenshots;
    assert(half_to_float(0x3c00) == 1.f && half_to_float(0xc000) == -2.f);
    assert(half_to_float(1) == std::ldexp(1.f,-24));
    assert(std::isinf(half_to_float(0x7c00)) && std::isnan(half_to_float(0x7e00)));
    assert(std::fabs(pq_nits(.5080784f)-100.f) < .01f);
    assert(std::fabs(pq_nits(.7518271f)-1000.f) < .1f);
    assert(std::fabs(pq_nits(1.f)-10000.f) < 2.f);
    float pixels[32] = {.18f,.5f,1.f,1.f, 4.f,2.f,.25f,1.f, -.5f,.02f,12.5f,1.f, 0,0,0,1,
        1,0,0,1, 0,1,0,1, 0,0,1,1, .25f,.25f,.25f,1};
    Image image{reinterpret_cast<unsigned char *>(pixels),4,2,64,2,Encoding::linear709};
    wchar_t path[1024];
    std::swprintf(path,1024,L"%s/master.exr",argv[1]); assert(write_exr(path,image));
    assert(!write_exr(path,image)); // never overwrite an earlier capture
    std::swprintf(path,1024,L"%s/preview.png",argv[1]); assert(write_png(path,image,true));
    assert(!write_png(path,image,true));
    image.encoding = Encoding::pq2020;
    const auto light = linear709({.7518271f,.7518271f,.7518271f},Encoding::pq2020);
    assert(std::fabs(light.r-12.5f)<.01f && std::fabs(light.g-12.5f)<.01f && std::fabs(light.b-12.5f)<.01f);
    unsigned char bgra[] = {0,128,255,0};
    const auto c = decode_pixel(bgra,87); assert(c.r == 1.f && c.b == 0 && c.g > .5f);
    std::puts("Screenshot codecs: PNG/EXR written; half/PQ/color decoding, HDR range and overwrite refusal passed.");
}
