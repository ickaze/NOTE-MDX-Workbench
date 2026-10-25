#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace mdx {
// Independent phase vocoder. Decoded PCM provides lookahead, so the sequencer
// and sample cursor need no latency compensation. Four Hann windows overlap.
class PcmPitchShift {
    static constexpr int N = 512, H = N / 4;
    static constexpr double pi = 3.14159265358979323846;
    std::array<double, N> overlap{};
    std::array<double, H> previous{};
    std::array<double, N / 2 + 1> lastPhase{}, phase{};
    int64_t base = 0;
    bool valid = false, first = true;
    static void fft(std::array<std::complex<double>, N> &a, bool inverse) {
        for (int i = 1, j = 0; i < N; ++i) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(a[i], a[j]);
        }
        for (int len = 2; len <= N; len <<= 1) {
            auto step = std::polar(1.0, (inverse ? 2 : -2) * pi / len);
            for (int i = 0; i < N; i += len) {
                std::complex<double> w(1, 0);
                for (int j = 0; j < len / 2; ++j) {
                    auto u = a[i + j], v = a[i + j + len / 2] * w;
                    a[i + j] = u + v;
                    a[i + j + len / 2] = u - v;
                    w *= step;
                }
            }
        }
        if (inverse)
            for (auto &v : a)
                v /= N;
    }
    void frame(const std::vector<int16_t> &pcm, int64_t start, double ratio) {
        static const auto window = [] {
            std::array<double, N> w{};
            for (int i = 0; i < N; ++i)
                w[i] = .5 - .5 * std::cos(2 * pi * i / N);
            return w;
        }();
        std::array<std::complex<double>, N> spectrum{};
        std::array<double, N / 2 + 1> magnitude{}, frequency{};
        for (int i = 0; i < N; ++i) {
            int64_t at = start + i;
            if (at >= 0 && at < int64_t(pcm.size()))
                spectrum[i] = pcm[size_t(at)] * window[i];
        }
        fft(spectrum, false);
        for (int k = 0; k <= N / 2; ++k) {
            double angle = std::arg(spectrum[k]), amplitude = std::abs(spectrum[k]);
            double delta = first ? 0 : std::remainder(angle - lastPhase[k] - 2 * pi * k * H / N, 2 * pi);
            lastPhase[k] = angle;
            int target = int(std::lround(k * ratio));
            if (target <= N / 2) {
                magnitude[target] += amplitude;
                frequency[target] += amplitude * (k + delta * N / (2 * pi * H)) * ratio;
            }
        }
        first = false;
        spectrum.fill({});
        for (int k = 0; k <= N / 2; ++k) {
            if (magnitude[k] > 1e-12)
                phase[k] = std::remainder(phase[k] + 2 * pi * H / N * frequency[k] / magnitude[k], 2 * pi);
            spectrum[k] = std::polar(magnitude[k], phase[k]);
            if (k == 0 || k == N / 2)
                spectrum[k] = spectrum[k].real();
            else
                spectrum[N - k] = std::conj(spectrum[k]);
        }
        fft(spectrum, true);
        for (int i = 0; i < N; ++i) {
            int64_t at = start + i - base;
            if (at >= 0 && at < N)
                overlap[size_t(at)] += spectrum[i].real() * window[i] / 1.5;
        }
    }
    double sample(const std::vector<int16_t> &pcm, int64_t at, double ratio) {
        while (at >= base + H) {
            std::copy_n(overlap.begin(), H, previous.begin());
            std::move(overlap.begin() + H, overlap.end(), overlap.begin());
            std::fill(overlap.end() - H, overlap.end(), 0);
            base += H;
            frame(pcm, base, ratio);
        }
        return at < base ? previous[size_t(at - base + H)] : overlap[size_t(at - base)];
    }

  public:
    void reset() { valid = false; }
    double read(const std::vector<int16_t> &pcm, double position, int shift, int mode) {
        if (pcm.empty() || position < 0 || position >= pcm.size())
            return 0;
        size_t a = size_t(position), b = std::min(a + 1, pcm.size() - 1);
        if (mode != 2 || shift == 0) {
            reset();
            return pcm[a] + (pcm[b] - pcm[a]) * (position - a);
        }
        double ratio = std::exp2(shift / 12.0);
        if (!valid || int64_t(a) < base - H || int64_t(a) >= base + N) {
            overlap.fill(0);
            previous.fill(0);
            lastPhase.fill(0);
            phase.fill(0);
            base = int64_t(a / H) * H;
            first = true;
            for (int i = 3; i >= 0; --i)
                frame(pcm, base - i * H, ratio);
            valid = true;
        }
        double x = sample(pcm, int64_t(a), ratio), y = sample(pcm, int64_t(b), ratio);
        return x + (y - x) * (position - a);
    }
};
} // namespace mdx
