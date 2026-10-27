// Small DSP building blocks for the Red Sun plugin.
// Filters follow the RBJ "Audio EQ Cookbook" formulas and run in double precision.
#pragma once

#include <cmath>

namespace redsun {

constexpr double kPi = 3.14159265358979323846;

inline double db_to_lin(double db) { return std::pow(10.0, db / 20.0); }

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0; }

    void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        b2 = nb2 / na0;
        a1 = na1 / na0;
        a2 = na2 / na0;
    }

    void lowpass(double fs, double f, double q) {
        const double w = 2.0 * kPi * f / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        set((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void highpass(double fs, double f, double q) {
        const double w = 2.0 * kPi * f / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        set((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void peak(double fs, double f, double q, double gain_db) {
        const double A = std::pow(10.0, gain_db / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        set(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    // Shelves use slope S = 1.
    void lowshelf(double fs, double f, double gain_db) {
        const double A = std::pow(10.0, gain_db / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos(w);
        const double k = 2.0 * std::sqrt(A) * std::sin(w) / 2.0 * std::sqrt(2.0);
        set(A * ((A + 1.0) - (A - 1.0) * c + k),
            2.0 * A * ((A - 1.0) - (A + 1.0) * c),
            A * ((A + 1.0) - (A - 1.0) * c - k),
            (A + 1.0) + (A - 1.0) * c + k,
            -2.0 * ((A - 1.0) + (A + 1.0) * c),
            (A + 1.0) + (A - 1.0) * c - k);
    }

    void highshelf(double fs, double f, double gain_db) {
        const double A = std::pow(10.0, gain_db / 40.0);
        const double w = 2.0 * kPi * f / fs, c = std::cos(w);
        const double k = 2.0 * std::sqrt(A) * std::sin(w) / 2.0 * std::sqrt(2.0);
        set(A * ((A + 1.0) + (A - 1.0) * c + k),
            -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
            A * ((A + 1.0) + (A - 1.0) * c - k),
            (A + 1.0) - (A - 1.0) * c + k,
            2.0 * ((A - 1.0) - (A + 1.0) * c),
            (A + 1.0) - (A - 1.0) * c - k);
    }
};

struct OnePoleLP {
    double a = 0.0, y = 0.0;
    void setup(double fs, double f) { a = std::exp(-2.0 * kPi * f / fs); }
    inline double process(double x) { return y = x + a * (y - x); }
    void reset() { y = 0.0; }
};

struct OnePoleHP {
    OnePoleLP lp;
    void setup(double fs, double f) { lp.setup(fs, f); }
    inline double process(double x) { return x - lp.process(x); }
    void reset() { lp.reset(); }
};

// 4x oversampling with 8th-order Butterworth anti-imaging / anti-aliasing filters.
class Oversampler4 {
public:
    static constexpr int kFactor = 4;

    void setup(double base_fs) {
        static const double q[4] = {0.50980, 0.60134, 0.89998, 2.56292};
        const double fc = 0.4 * base_fs;
        for (int i = 0; i < 4; ++i) {
            up_[i].lowpass(base_fs * kFactor, fc, q[i]);
            down_[i].lowpass(base_fs * kFactor, fc, q[i]);
        }
    }

    void reset() {
        for (auto &f : up_) f.reset();
        for (auto &f : down_) f.reset();
    }

    // Zero-stuffs one input sample into kFactor filtered samples.
    inline void upsample(double x, double *out) {
        for (int n = 0; n < kFactor; ++n) {
            double s = (n == 0) ? x * kFactor : 0.0;
            for (auto &f : up_) s = f.process(s);
            out[n] = s;
        }
    }

    inline double downsample(const double *in) {
        double s = 0.0;
        for (int n = 0; n < kFactor; ++n) {
            s = in[n];
            for (auto &f : down_) s = f.process(s);
        }
        return s;
    }

private:
    Biquad up_[4], down_[4];
};

}  // namespace redsun
