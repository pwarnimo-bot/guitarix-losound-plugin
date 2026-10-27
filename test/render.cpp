// Offline test harness: loads the plugin binary, runs a synthetic C-standard
// riff through it, checks the output for sanity and writes WAV files.
#include <lv2/core/lv2.h>

#include <dlfcn.h>
#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static const double kFs = 48000.0;

struct Settings {
    const char *name;
    float v[9];  // fuzz octave bass mid treble ultralo sag cab level
};

static std::vector<float> pluck(double freq, double seconds, double amp, std::mt19937 &rng) {
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    const int period = static_cast<int>(kFs / freq + 0.5);
    // Low-passed excitation approximates a pick on wound strings rather than white noise.
    std::vector<double> ring(period);
    double lp = 0.0;
    for (int pass = 0; pass < 2; ++pass)
        for (auto &r : ring) r = lp += 0.25 * ((pass ? r : noise(rng)) - lp);
    std::vector<float> out(static_cast<size_t>(seconds * kFs));
    double prev = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t k = i % period;
        const double s = ring[k];
        ring[k] = 0.996 * 0.5 * (s + prev);
        prev = s;
        out[i] = static_cast<float>(s * amp);
    }
    return out;
}

static void mix_in(std::vector<float> &dst, const std::vector<float> &src, double at) {
    const size_t off = static_cast<size_t>(at * kFs);
    for (size_t i = 0; i < src.size() && off + i < dst.size(); ++i) dst[off + i] += src[i];
}

// C standard: low C power chords, a chug, then a high lead line for the octave effect.
static std::vector<float> make_riff() {
    std::mt19937 rng(1992);
    std::vector<float> sig(static_cast<size_t>(8.0 * kFs), 0.0f);
    const double C2 = 65.41, G2 = 98.0, C3 = 130.81, Eb2 = 77.78, Bb2 = 116.54, F2 = 87.31;
    auto chord = [&](double root, double at, double len) {
        mix_in(sig, pluck(root, len, 0.12, rng), at);
        mix_in(sig, pluck(root * 1.4983, len, 0.10, rng), at + 0.004);
        mix_in(sig, pluck(root * 2.0, len, 0.08, rng), at + 0.008);
    };
    chord(C2, 0.0, 1.5);
    chord(Eb2, 1.5, 0.5);
    chord(F2, 2.0, 1.0);
    for (int i = 0; i < 8; ++i) mix_in(sig, pluck(C2, 0.18, 0.15, rng), 3.0 + i * 0.2);
    const double lead[] = {C3 * 4, Eb2 * 8, F2 * 8, G2 * 4, Bb2 * 4, C3 * 4};
    for (int i = 0; i < 6; ++i) mix_in(sig, pluck(lead[i], 0.6, 0.2, rng), 4.8 + i * 0.5);
    return sig;
}

static void write_wav(const std::string &path, const std::vector<float> &data) {
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint32_t n = data.size(), rate = kFs, bytes = n * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (float s : data) {
        const double c = std::fmax(-1.0, std::fmin(1.0, s));
        u16(static_cast<uint16_t>(static_cast<int16_t>(c * 32767.0)));
    }
    std::fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s plugin.so outdir\n", argv[0]);
        return 2;
    }
    void *lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto get = reinterpret_cast<LV2_Descriptor_Function>(dlsym(lib, "lv2_descriptor"));
    if (!get || !get(0) || get(1)) { std::fprintf(stderr, "bad descriptor\n"); return 1; }
    const LV2_Descriptor *d = get(0);
    std::printf("plugin: %s\n", d->URI);
    mkdir(argv[2], 0755);

    const std::vector<float> riff = make_riff();
    write_wav(std::string(argv[2]) + "/dry.wav", riff);

    const Settings settings[] = {
        {"default",       {0.75f, 0.2f, 0.65f, 0.35f, 0.55f, 1, 0.5f, 0.15f, -6}},
        {"green_machine", {0.8f, 0.15f, 0.7f, 0.3f, 0.55f, 1, 0.6f, 0.1f, -8}},
        {"thumb",         {0.6f, 0.0f, 0.6f, 0.5f, 0.45f, 1, 0.75f, 0.0f, -6}},
        {"freedom_run",   {0.9f, 0.6f, 0.55f, 0.6f, 0.6f, 0, 0.4f, 0.25f, -8}},
        {"bass_cab_only", {0.75f, 0.2f, 0.65f, 0.35f, 0.55f, 1, 0.5f, 0.0f, -6}},
        {"guitar_cab_only",{0.75f, 0.2f, 0.65f, 0.35f, 0.55f, 1, 0.5f, 1.0f, -6}},
        {"all_min",       {0, 0, 0, 0, 0, 0, 0, 0, -30}},
        {"all_max",       {1, 1, 1, 1, 1, 1, 1, 1, 12}},
    };

    int failures = 0;
    for (const Settings &s : settings) {
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            LV2_Handle h = d->instantiate(d, rate, "", nullptr);
            float controls[9];
            for (int i = 0; i < 9; ++i) controls[i] = s.v[i];
            std::vector<float> out(riff.size());
            for (int i = 0; i < 9; ++i) d->connect_port(h, 2 + i, &controls[i]);
            d->activate(h);

            // Vary block sizes to catch buffer handling bugs.
            const uint32_t blocks[] = {1, 64, 256, 1000, 4096};
            size_t pos = 0;
            int bi = 0;
            const auto t0 = std::chrono::steady_clock::now();
            while (pos < riff.size()) {
                uint32_t n = blocks[bi++ % 5];
                if (pos + n > riff.size()) n = riff.size() - pos;
                d->connect_port(h, 0, const_cast<float *>(&riff[pos]));
                d->connect_port(h, 1, &out[pos]);
                d->run(h, n);
                pos += n;
            }
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            d->cleanup(h);

            double peak = 0, sum = 0;
            bool finite = true;
            for (float v : out) {
                if (!std::isfinite(v)) finite = false;
                peak = std::fmax(peak, std::fabs(v));
                sum += double(v) * v;
            }
            const double rms_db = 10 * std::log10(sum / out.size() + 1e-30);
            // Peak relative to the output Level setting: catches runaway gain or instability.
            const double rel_peak = peak / std::pow(10.0, s.v[8] / 20.0);
            const bool ok = finite && rel_peak < 2.0;
            if (!ok) ++failures;
            std::printf("%-16s %6.0f Hz  peak %6.3f  rms %6.1f dBFS  cpu %5.2f%%  %s\n", s.name, rate, peak, rms_db,
                        100.0 * secs / (riff.size() / kFs), ok ? "ok" : "FAIL");
            if (rate == 48000.0) write_wav(std::string(argv[2]) + "/" + s.name + ".wav", out);
        }
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
