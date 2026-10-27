// LoSound Red Sun: desert-rock fuzz into a bass amp and bass cabinet.
//
// Signal chain:
//   input filters -> [4x oversampled: two-stage fuzz with octave-up rectifier
//   -> bass-amp tone stack -> sagging power stage] -> cabinet sim -> level
#include <lv2/core/lv2.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

#include "dsp.h"

#define REDSUN_URI "urn:losound:redsun"

namespace redsun {

enum Port {
    kIn = 0,
    kOut,
    kFuzz,
    kOctave,
    kBass,
    kMid,
    kTreble,
    kUltraLo,
    kSag,
    kCab,
    kLevel,
    kNumPorts
};

enum Param { pFuzz, pOctave, pBass, pMid, pTreble, pUltraLo, pSag, pCab, pLevel, kNumParams };

static const float kDefaults[kNumParams] = {0.75f, 0.2f, 0.65f, 0.35f, 0.55f, 1.0f, 0.5f, 0.15f, -6.0f};
static const float kMin[kNumParams] = {0, 0, 0, 0, 0, 0, 0, 0, -30};
static const float kMax[kNumParams] = {1, 1, 1, 1, 1, 1, 1, 1, 12};

class RedSun {
public:
    explicit RedSun(double fs) : fs_(fs), osfs_(fs * Oversampler4::kFactor) {
        for (auto &p : ports_) p = nullptr;
        for (int i = 0; i < kNumParams; ++i) smooth_[i] = kDefaults[i];
        // ~30 ms parameter smoothing.
        smooth_coef_ = 1.0 - std::exp(-1.0 / (0.03 * fs_));

        os_.setup(fs_);

        dc_in_.setup(fs_, 20.0);
        tight_.highpass(fs_, 70.0, 0.6);
        pickup_lp_.lowpass(fs_, std::min(6500.0, 0.45 * fs_), 0.7);

        interstage_lp_.setup(osfs_, 6000.0);
        oct_dc_.setup(osfs_, 20.0);
        fuzz_lp_.lowpass(osfs_, 3500.0, 0.6);
        fuzz_dc_.setup(osfs_, 15.0);

        env_att_ = 1.0 - std::exp(-1.0 / (0.010 * osfs_));
        env_rel_ = 1.0 - std::exp(-1.0 / (0.180 * osfs_));

        setup_cabs();
        out_dc_.setup(fs_, 10.0);

        update_tone();
    }

    void connect(uint32_t port, void *data) {
        if (port < kNumPorts) ports_[port] = static_cast<float *>(data);
    }

    void activate() {
        os_.reset();
        dc_in_.reset();
        tight_.reset();
        pickup_lp_.reset();
        interstage_lp_.reset();
        oct_dc_.reset();
        fuzz_lp_.reset();
        fuzz_dc_.reset();
        bass_.reset();
        mid_.reset();
        treble_.reset();
        ul_shelf_.reset();
        ul_mid_.reset();
        for (auto &f : bass_cab_) f.reset();
        for (auto &f : guitar_cab_) f.reset();
        out_dc_.reset();
        env_ = 0.0;
        for (int i = 0; i < kNumParams; ++i) smooth_[i] = target(i);
        update_tone();
    }

    void run(uint32_t nframes) {
        const float *in = ports_[kIn];
        float *out = ports_[kOut];
        if (!in || !out) return;

#if defined(__SSE__)
        const unsigned int old_csr = _mm_getcsr();
        _mm_setcsr(old_csr | 0x8040);  // flush-to-zero + denormals-are-zero
#endif
        double targets[kNumParams];
        for (int i = 0; i < kNumParams; ++i) targets[i] = target(i);

        for (uint32_t i = 0; i < nframes; ++i) {
            for (int p = 0; p < kNumParams; ++p) smooth_[p] += smooth_coef_ * (targets[p] - smooth_[p]);
            if (++tone_counter_ >= kToneInterval) {
                tone_counter_ = 0;
                update_tone();
            }
            out[i] = static_cast<float>(process(in[i]));
        }

#if defined(__SSE__)
        _mm_setcsr(old_csr);
#endif
    }

private:
    static constexpr int kToneInterval = 32;

    double target(int p) const {
        const float *port = ports_[kFuzz + p];
        const float v = port ? *port : kDefaults[p];
        if (!std::isfinite(v)) return kDefaults[p];
        return std::clamp(v, kMin[p], kMax[p]);
    }

    void setup_cabs() {
        const double ny = 0.45 * fs_;
        // Ampeg-style 8x10 bass cabinet: deep low end, no tweeter, steep top roll-off.
        bass_cab_[0].highpass(fs_, 40.0, 0.9);
        bass_cab_[1].peak(fs_, 120.0, 0.7, 4.0);
        bass_cab_[2].peak(fs_, 450.0, 0.9, -2.0);
        bass_cab_[3].peak(fs_, 2000.0, 1.0, -2.0);
        bass_cab_[4].lowpass(fs_, std::min(2800.0, ny), 0.7);
        bass_cab_[5].lowpass(fs_, std::min(4500.0, ny), 0.6);
        // Closed-back 4x12 guitar cabinet for blending in some bite.
        guitar_cab_[0].highpass(fs_, 75.0, 1.1);
        guitar_cab_[1].peak(fs_, 110.0, 1.0, 2.0);
        guitar_cab_[2].peak(fs_, 700.0, 1.0, -3.0);
        guitar_cab_[3].peak(fs_, 2800.0, 1.4, 5.0);
        guitar_cab_[4].lowpass(fs_, std::min(5500.0, ny), 0.7);
        guitar_cab_[5].lowpass(fs_, std::min(7000.0, ny), 0.6);
    }

    void update_tone() {
        const double b = smooth_[pBass], m = smooth_[pMid], t = smooth_[pTreble], ul = smooth_[pUltraLo];
        if (b == tone_b_ && m == tone_m_ && t == tone_t_ && ul == tone_ul_) return;
        tone_b_ = b;
        tone_m_ = m;
        tone_t_ = t;
        tone_ul_ = ul;
        bass_.lowshelf(osfs_, 100.0, (b - 0.5) * 30.0);
        mid_.peak(osfs_, 500.0, 0.7, (m - 0.5) * 24.0);
        treble_.highshelf(osfs_, 2500.0, (t - 0.5) * 30.0);
        // Ultra Lo: a little extra sub and a deep 500 Hz notch, like the Ampeg switch.
        ul_shelf_.lowshelf(osfs_, 45.0, ul * 3.0);
        ul_mid_.peak(osfs_, 500.0, 1.0, ul * -6.0);
    }

    inline double process(double x) {
        x = dc_in_.process(x);
        x = tight_.process(x);
        x = pickup_lp_.process(x);

        const double fuzz = smooth_[pFuzz];
        const double octave = smooth_[pOctave];
        const double sag = smooth_[pSag];
        const double stage_gain = db_to_lin((6.0 + 44.0 * fuzz) * 0.5);
        const double supply_k = 3.0 * sag;
        const double drive = 0.8 * (1.0 + 1.5 * sag);
        const double makeup = 1.0 + 0.5 * sag;

        double buf[Oversampler4::kFactor];
        os_.upsample(x, buf);
        for (double &s : buf) {
            // Fuzz stage 1: biased soft clip for even harmonics.
            double s1 = std::tanh(stage_gain * s + kBias1) - kTanhBias1;
            s1 = interstage_lp_.process(s1);
            // Octave up: full-wave rectified copy of stage 1.
            const double oct = oct_dc_.process(std::fabs(s1));
            const double mix = s1 * (1.0 - 0.3 * octave) + 1.4 * octave * oct;
            // Fuzz stage 2: harder clip, then soften the fizz.
            double s2 = std::tanh(stage_gain * mix * 1.5 - kBias2) + kTanhBias2;
            s2 = fuzz_dc_.process(fuzz_lp_.process(s2)) * 0.6;

            // Bass amp tone stack.
            double t = bass_.process(s2);
            t = mid_.process(t);
            t = treble_.process(t);
            t = ul_shelf_.process(t);
            t = ul_mid_.process(t);

            // Power stage: rail voltage drops with signal level, so loud notes squash and bloom.
            const double level = std::fabs(t);
            env_ += (level > env_ ? env_att_ : env_rel_) * (level - env_);
            const double supply = 1.0 / (1.0 + supply_k * env_);
            s = supply * std::tanh(drive * t / supply) * makeup;
        }
        const double amp = os_.downsample(buf);

        double bass = amp, guitar = amp;
        for (auto &f : bass_cab_) bass = f.process(bass);
        for (auto &f : guitar_cab_) guitar = f.process(guitar);
        const double cab = smooth_[pCab];
        const double y = (1.0 - cab) * bass * kBassCabGain + cab * guitar * kGuitarCabGain;

        return out_dc_.process(y) * db_to_lin(smooth_[pLevel]);
    }

    static constexpr double kBias1 = 0.25;
    static constexpr double kBias2 = 0.1;
    const double kTanhBias1 = std::tanh(kBias1);
    const double kTanhBias2 = std::tanh(kBias2);
    static constexpr double kBassCabGain = 1.0;
    static constexpr double kGuitarCabGain = 1.0;

    float *ports_[kNumPorts];
    double fs_, osfs_;
    double smooth_[kNumParams];
    double smooth_coef_;
    int tone_counter_ = 0;
    double tone_b_ = -1, tone_m_ = -1, tone_t_ = -1, tone_ul_ = -1;

    Oversampler4 os_;
    OnePoleHP dc_in_;
    Biquad tight_, pickup_lp_;
    OnePoleLP interstage_lp_;
    OnePoleHP oct_dc_;
    Biquad fuzz_lp_;
    OnePoleHP fuzz_dc_;
    Biquad bass_, mid_, treble_, ul_shelf_, ul_mid_;
    double env_ = 0.0, env_att_, env_rel_;
    Biquad bass_cab_[6], guitar_cab_[6];
    OnePoleHP out_dc_;
};

}  // namespace redsun

static LV2_Handle instantiate(const LV2_Descriptor *, double rate, const char *, const LV2_Feature *const *) {
    return new redsun::RedSun(rate);
}

static void connect_port(LV2_Handle h, uint32_t port, void *data) {
    static_cast<redsun::RedSun *>(h)->connect(port, data);
}

static void activate(LV2_Handle h) { static_cast<redsun::RedSun *>(h)->activate(); }

static void run(LV2_Handle h, uint32_t n) { static_cast<redsun::RedSun *>(h)->run(n); }

static void cleanup(LV2_Handle h) { delete static_cast<redsun::RedSun *>(h); }

static const void *extension_data(const char *) { return nullptr; }

static const LV2_Descriptor descriptor = {
    REDSUN_URI, instantiate, connect_port, activate, run, nullptr, cleanup, extension_data,
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
