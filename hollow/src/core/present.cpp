// Presenting the finished canvas at the window's scale: the scale limits, what a scale does to a size, and the
// resampler the platform layers call to put (a region of) the canvas into the window.
//
// The canvas is drawn once, at scale 1, whatever the window size; every coordinate, font and hit area of a skin
// lives in that space. A whole scale replicates pixels exactly as the runtime always did, so the artwork is
// untouched at 1x, 2x, 3x and 4x. Any other scale resamples: up, a "sharp bilinear" blend whose soft edge is one
// window pixel wide (pixel art stays crisp and still has no uneven pixel columns), and down, the exact average
// of the source area each window pixel covers.
#include "core.h"
#include <algorithm>
#include <cmath>

namespace hollow {

double clampScale(double s) {
    if (!(s == s)) return 1.0;   // NaN
    s = std::clamp(s, kMinScale, kMaxScale);
    const double whole = std::round(s);
    return std::fabs(s - whole) <= kSnapScale ? whole : s;
}

bool wholeScale(double s) { return std::fabs(s - std::round(s)) < 1e-6; }

std::string scaleText(double scale) {
    return wholeScale(scale) ? std::to_string((long)std::lround(scale)) + "x" : std::to_string((long)std::lround(scale * 100)) + "%";
}

int scaledSize(int canvas, double scale) { return std::max(1, (int)std::lround(canvas * scale)); }

Rect windowRect(Rect r, double scale, int width, int height) {
    if (r.empty()) return {};
    Rect out;
    if (wholeScale(scale)) {
        const int s = (int)std::lround(scale);
        out = {r.x * s, r.y * s, r.w * s, r.h * s};
    } else {   // a smoothed pixel reaches one source pixel past its own
        const int x0 = (int)std::floor((r.x - 1) * scale), y0 = (int)std::floor((r.y - 1) * scale);
        const int x1 = (int)std::ceil((r.x + r.w + 1) * scale), y1 = (int)std::ceil((r.y + r.h + 1) * scale);
        out = {x0, y0, x1 - x0, y1 - y0};
    }
    return out & Rect{0, 0, width, height};
}

namespace {

// a * (256 - w) + b * w, per channel, w in 0..256; the alpha byte comes out 0.
inline uint32_t mix(uint32_t a, uint32_t b, uint32_t w) {
    const uint32_t rb = (((a & 0x00ff00ffu) * (256 - w) + (b & 0x00ff00ffu) * w) >> 8) & 0x00ff00ffu;
    const uint32_t g = (((a & 0x0000ff00u) * (256 - w) + (b & 0x0000ff00u) * w) >> 8) & 0x0000ff00u;
    return rb | g;
}

struct Tap {   // one window column or row, up scaling: two source pixels and the weight of the second (0..256)
    int a, b;
    uint32_t w;
};

// The taps of the window pixels first .. first + n - 1 on one axis, `scale` window pixels per source pixel
// (the axis' own, so the image fills the window exactly) over `size` source pixels.
void upTaps(Tap* t, int first, int n, double scale, int size) {
    for (int k = 0; k < n; ++k) {
        const double u = (first + k + 0.5) / scale - 0.5;   // the source coordinate, pixel centres on whole numbers
        const double f0 = std::floor(u);
        const double f = u - f0;
        const double sharp = std::clamp((f - 0.5) * scale + 0.5, 0.0, 1.0);   // the blend is one window pixel wide
        const int i = (int)f0;
        t[k] = {std::clamp(i, 0, size - 1), std::clamp(i + 1, 0, size - 1), (uint32_t)std::lround(sharp * 256)};
    }
}

} // namespace

void presentScaled(const uint32_t* src, int sw, int sh, int W, int H, Rect r, uint32_t* dst) {
    if (!src || sw <= 0 || sh <= 0 || r.empty() || r.x < 0 || r.y < 0 || r.x + r.w > W || r.y + r.h > H) return;

    if (W % sw == 0 && W / sw >= 1 && H == sh * (W / sw)) {   // a whole scale: the pixels as they are, replicated
        const int s = W / sw;
        for (int y = 0; y < r.h; ++y) {
            const uint32_t* row = src + (size_t)((r.y + y) / s) * sw;
            uint32_t* d = dst + (size_t)y * r.w;
            for (int x = 0; x < r.w; ++x) d[x] = row[(r.x + x) / s];
        }
        return;
    }

    const double sx = (double)W / sw, sy = (double)H / sh;   // window pixels per source pixel, per axis

    if (sx + sy > 2.0) {
        std::vector<Tap> cx((size_t)r.w), cy((size_t)r.h);
        upTaps(cx.data(), r.x, r.w, sx, sw);
        upTaps(cy.data(), r.y, r.h, sy, sh);
        for (int y = 0; y < r.h; ++y) {
            const Tap& ty = cy[(size_t)y];
            const uint32_t* r0 = src + (size_t)ty.a * sw;
            const uint32_t* r1 = src + (size_t)ty.b * sw;
            uint32_t* d = dst + (size_t)y * r.w;
            for (int x = 0; x < r.w; ++x) {
                const Tap& tx = cx[(size_t)x];
                d[x] = 0xff000000u | mix(mix(r0[tx.a], r0[tx.b], tx.w), mix(r1[tx.a], r1[tx.b], tx.w), ty.w);
            }
        }
        return;
    }

    // Down: each window pixel is the average of the source area it covers.
    struct Span {
        int a, b;      // first and last source pixel touched
        double w0, w1; // the coverage of the first and of the last (the ones between are covered whole)
    };
    auto spans = [](std::vector<Span>& v, int first, int n, double scale, int size) {
        v.resize((size_t)n);
        for (int k = 0; k < n; ++k) {
            const double lo = (first + k) / scale, hi = (first + k + 1) / scale;
            int a = std::clamp((int)std::floor(lo), 0, size - 1), b = std::clamp((int)std::ceil(hi) - 1, 0, size - 1);
            if (b < a) b = a;
            Span s{a, b, std::min(a + 1.0, hi) - std::max((double)a, lo), std::min(b + 1.0, hi) - std::max((double)b, lo)};
            if (a == b) s.w0 = s.w1 = std::max(hi - lo, 1e-9);
            v[(size_t)k] = s;
        }
    };
    std::vector<Span> cx, cy;
    spans(cx, r.x, r.w, sx, sw);
    spans(cy, r.y, r.h, sy, sh);
    for (int y = 0; y < r.h; ++y) {
        const Span& py = cy[(size_t)y];
        uint32_t* d = dst + (size_t)y * r.w;
        for (int x = 0; x < r.w; ++x) {
            const Span& px = cx[(size_t)x];
            double sr = 0, sg = 0, sb = 0, sum = 0;
            for (int j = py.a; j <= py.b; ++j) {
                const double wy = j == py.a ? py.w0 : j == py.b ? py.w1 : 1.0;
                const uint32_t* row = src + (size_t)j * sw;
                for (int i = px.a; i <= px.b; ++i) {
                    const double w = wy * (i == px.a ? px.w0 : i == px.b ? px.w1 : 1.0);
                    const uint32_t p = row[i];
                    sr += w * (p >> 16 & 255);
                    sg += w * (p >> 8 & 255);
                    sb += w * (p & 255);
                    sum += w;
                }
            }
            const uint32_t R = (uint32_t)std::lround(sr / sum), G = (uint32_t)std::lround(sg / sum), B = (uint32_t)std::lround(sb / sum);
            d[x] = 0xff000000u | R << 16 | G << 8 | B;
        }
    }
}

} // namespace hollow
