// hollow-render <skin_dir> <view> <out.png> [state file] [--scale S]: renders one view of a skin folder, as the
// runtime draws it at scale 1 with every value at its default; with --scale, as a window at that scale shows it.
#include "core/core.h"
#include "embedded_skin.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBIW_WINDOWS_UTF8
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hollow {
// The tool reads a folder; it has no embedded skin.
const EmbeddedFile kSkinFiles[] = {{"", nullptr, 0}};
const size_t kSkinFileCount = 0;
}

int main(int argc, char** argv) {
    // hollow-render <skin_dir> <view> <out.png> [state file] [--scale S]
    std::vector<std::string> args;
    double scale = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--scale" && i + 1 < argc) scale = std::atof(argv[++i]);
        else if (a.rfind("--scale=", 0) == 0) scale = std::atof(a.c_str() + 8);
        else args.push_back(a);
    }
    if (args.size() != 3 && args.size() != 4) {
        std::fprintf(stderr, "usage: hollow-render <skin_dir> <view> <out.png> [state file] [--scale S]\n");
        return 2;
    }
    std::string err;
    auto skin = hollow::loadSkinDir(args[0], &err);
    if (!err.empty()) std::fprintf(stderr, "%s", err.c_str());
    if (!skin) return 1;
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    std::string state;
    if (args.size() == 4) {   // a saved state (params, text data) to render with
        std::ifstream f(args[3], std::ios::binary);
        state.assign(std::istreambuf_iterator<char>(f), {});
    }
    if (!hollow::renderView(*skin, args[1], rgba, w, h, state)) {
        std::fprintf(stderr, "no view '%s' in %s\n", args[1].c_str(), args[0].c_str());
        return 1;
    }
    if (scale != 1) {   // shown in a window at that scale, as the editor presents it: the same resampler
        scale = hollow::clampScale(scale);
        std::vector<uint32_t> canvas((size_t)w * h);
        for (size_t i = 0; i < canvas.size(); ++i)
            canvas[i] = (uint32_t)rgba[i * 4 + 3] << 24 | (uint32_t)rgba[i * 4] << 16 | (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
        const int W = hollow::scaledSize(w, scale), H = std::max(1, (int)std::lround(h * (double)W / w));
        std::vector<uint32_t> out((size_t)W * H);
        hollow::presentScaled(canvas.data(), w, h, W, H, {0, 0, W, H}, out.data());
        rgba.resize(out.size() * 4);
        for (size_t i = 0; i < out.size(); ++i) {
            rgba[i * 4] = (uint8_t)(out[i] >> 16);
            rgba[i * 4 + 1] = (uint8_t)(out[i] >> 8);
            rgba[i * 4 + 2] = (uint8_t)out[i];
            rgba[i * 4 + 3] = (uint8_t)(out[i] >> 24);
        }
        w = W;
        h = H;
    }
    if (!stbi_write_png(args[2].c_str(), w, h, 4, rgba.data(), w * 4)) {
        std::fprintf(stderr, "cannot write %s\n", args[2].c_str());
        return 1;
    }
    return 0;
}
