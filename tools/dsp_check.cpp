// dsp_check - the filters' and effects' output, pinned. The six filter types with notes, controller offsets, a
// kill and a retake; every effect type of each block with every parameter randomised halfway. The hashes were what
// the firmware's own DSP programs computed on the chip interpreter for the same drive, bit for bit, before those
// programs moved to the VOP3 project. The filter's moved once since, when the sine LFO and the EG's k were rounded
// through float, which makes them the C library's no longer. The effects' moved once, when a window stopped
// inheriting the last program's state: this drive walks all 87 types in a row, so every one of those type changes
// is in the hash (docs/Differences.md, "A performance change starts the effect section over"). Both hashes are the
// same on Linux, macOS and Windows, 64- and 32-bit: a change here is a change in sound.
#include <cstdio>
#include <cstring>
#include <memory>
#include "effects/effects.h"
#include "filters/filter.h"
namespace H = fsvr::vop3host;
static uint32_t seed; static double noise() { seed = seed * 1664525u + 1013904223u; return (int32_t)seed * (0.2 / 2147483648.0); }
static uint64_t rng; static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return uint32_t(rng); }
static uint64_t h = 1469598103934665603ull;
static void eat(const void* p, size_t n) { auto b = (const unsigned char*)p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; }
int main() {
  seed = 12345; rng = 88172645463325252ull;
  { auto fb = std::make_unique<Filter::Bank>();
    for (int type = 0; type < 6; type++) {
      using namespace H::filter;
      uint8_t v[H::FilterHost::VOICE_BYTES] = {}, r[H::FilterHost::PART_BYTES] = {};
      for (int i = 0x18; i <= 0x23; i++) r[i] = 64;
      r[PART_LFO2_SPEED] = r[PART_LFO2_DEPTH] = 64;
      v[FILTER_TYPE] = (uint8_t)type; v[CUTOFF] = 70; v[RESONANCE] = 90; v[KEY_SCALE_DEPTH] = 64; v[KEY_SCALE_POINT] = 60;
      v[INPUT_GAIN] = 12; v[EG_DEPTH] = 90; v[CUTOFF_LFO2] = 40; v[LFO2_WAVE] = uint8_t(type % 6); v[LFO2_KEY_SYNC] = type & 1;
      for (int i = EG_L4; i <= EG_L3; i++) v[i] = 20 + 10 * (i - EG_L4);
      auto& f = *fb; f.reset(); for (int p = 0; p < 4; p++) f.sync(p, v, r, 0, 0, 0);
      for (int s = 0; s < 16; s++) f.host.noteOn(s, s & 3, 36 + 3 * s, 64 + 4 * s);
      const int n = 9600;
      for (int i = 0; i < n; i++) {
        if (i == n / 2) { v[CUTOFF] = uint8_t(rnd() % 128); v[RESONANCE] = uint8_t(rnd() % 128); const int mod = int(rnd() % 255) - 127;
          for (int p = 0; p < 4; p++) f.sync(p, v, r, mod, mod / 2, -mod / 3);
          for (int s = 0; s < 16; s += 2) f.host.noteOff(s);
          f.host.setSlotCutoffMod(3, mod); f.host.flush(); }
        if (i == 3 * n / 4) { f.host.kill(5); f.host.noteOn(5, 2, 77, 30); }
        double x[16]; for (int s = 0; s < 16; s++) x[s] = noise() + (i % 2400 == 0 ? 0.9 : 0.0);
        f.run(x); eat(x, sizeof x);
      } } }
  const uint64_t filter = h; h = 1469598103934665603ull;
  { auto e = std::make_unique<Effects>();
    uint8_t c[H::COMMON_BYTES] = {}; c[0x10] = 127; c[0x11] = 64; c[0x12] = 24;
    c[H::EQ_LOW_GAIN] = c[H::EQ_MID_GAIN] = c[H::EQ_HIGH_GAIN] = 64; c[H::REVERB_PAN] = c[H::VARIATION_PAN] = c[H::INSERTION_PAN] = 64;
    c[H::REVERB_RETURN] = c[H::VARIATION_RETURN] = 100; c[H::INSERTION_LEVEL] = 127;
    c[H::INSERTION_TO_REVERB] = c[H::INSERTION_TO_VARIATION] = c[H::VARIATION_TO_REVERB] = 40;
    e->load(c);
    const struct { uint32_t byte; int types; } blocks[] = {{H::REVERB_TYPE, 17}, {H::VARIATION_TYPE, 29}, {H::INSERTION_TYPE, 41}};
    for (auto& bl : blocks) for (int type = 0; type < bl.types; type++) {
      e->host.setByte(bl.byte, type);
      const int n = 2400;
      for (int i = 0; i < n; i++) {
        if (i == n / 2) { uint8_t cb[H::COMMON_BYTES]; std::memcpy(cb, e->host.perf, sizeof cb);
          for (uint32_t k = H::REVERB_PARAM_BYTES; k <= H::EQ_HIGH_SHAPE; k++) if (k != H::REVERB_TYPE && k != H::VARIATION_TYPE && k != H::INSERTION_TYPE) cb[k] = uint8_t(rnd() % 128);
          e->sync(cb); }
        const double x = noise() + (i % 4800 == 0 ? 0.9 : 0.0);
        const double bus[8] = {x, -x, x, 0.5 * x, x, x, -x, x};
        double lr[2]; e->run(bus, lr[0], lr[1]); eat(lr, sizeof lr);
      } } }
  const uint64_t effects = h;
  const bool ok = filter == 0x0d3a65cd60d920adull && effects == 0x24e70176e44dbff3ull;
  std::printf("filter %016llx, effects %016llx: %s\n", (unsigned long long)filter, (unsigned long long)effects, ok ? "ok" : "CHANGED");
  return ok ? 0 : 1;
}
