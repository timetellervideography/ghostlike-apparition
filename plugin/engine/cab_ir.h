// Cab impulse responses: minimum-phase speaker + mic + position, port of cabIR() from the web preview (same seeds, same result).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace gl {

struct Mulberry {  // mulberry32, bit-identical to the JS version
  uint32_t a;
  explicit Mulberry(uint32_t s) : a(s) {}
  double operator()() {
    a += 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
  }
};

struct CabDef { std::vector<std::pair<double, double>> pts; uint32_t seed = 3; double tail0 = .012, tail1 = 2.5; };

inline double lg2(double x) { return std::log2(x); }
inline double gauss(double f, double fc, double w) { double v = lg2(f / fc) / w; return std::exp(-v * v); }
inline double sstep(double x) { return x <= 0 ? 0 : x >= 1 ? 1 : x * x * (3 - 2 * x); }

inline double curveDB(const std::vector<std::pair<double, double>>& p, double f) {
  if (f <= p[0].first) return p[0].second - lg2(p[0].first / std::max(f, 1.0)) * 12;
  for (size_t i = 0; i + 1 < p.size(); i++)
    if (f < p[i + 1].first) { double t = std::log(f / p[i].first) / std::log(p[i + 1].first / p[i].first); return p[i].second + t * (p[i + 1].second - p[i].second); }
  return p.back().second;
}

struct Ripple {
  std::vector<std::pair<double, double>> n;
  explicit Ripple(uint32_t seed) { Mulberry r(seed); for (double f = 700; f < 12000; f *= std::pow(2.0, 1.0 / 5)) n.push_back({f, (r() * 2 - 1) * 2.8}); }
  double operator()(double f) const {
    if (f < 700 || f >= n.back().first) return 0;
    size_t i = 0; while (n[i + 1].first <= f) i++;
    double t = std::log(f / n[i].first) / std::log(n[i + 1].first / n[i].first), s = (1 - std::cos(M_PI * t)) / 2;
    return n[i].second + s * (n[i + 1].second - n[i].second);
  }
};

// mic 0 = dynamic 57, 1 = ribbon 121
inline double micDB(int mic, double f, double p) {
  double d = 0;
  if (mic == 0) d += 3 * gauss(f, 5200, .6) + 1.5 * gauss(f, 160, .8) - (f < 70 ? lg2(70 / f) * 6 : 0) - (f > 11000 ? lg2(f / 11000) * 18 : 0);
  else d += -7 * sstep(lg2(f / 2200) / 1.6) + 3 * gauss(f, 120, .9) - (f > 8000 ? lg2(f / 8000) * 12 : 0);
  return d - p * 12 * sstep(lg2(f / 1400) / 2) + p * 2 * gauss(f, 300, 1);
}

inline void fft(std::vector<double>& re, std::vector<double>& im, bool inv) {
  const size_t n = re.size();
  for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
  for (size_t len = 2; len <= n; len <<= 1) {
    double a = 2 * M_PI / len * (inv ? 1 : -1), wr = std::cos(a), wi = std::sin(a);
    for (size_t i = 0; i < n; i += len) {
      double cr = 1, ci = 0;
      for (size_t j = 0; j < len / 2; j++) {
        size_t k = i + j + len / 2; double ar = re[k] * cr - im[k] * ci, ai = re[k] * ci + im[k] * cr;
        re[k] = re[i + j] - ar; im[k] = im[i + j] - ai; re[i + j] += ar; im[i + j] += ai;
        double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
  if (inv) for (size_t i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

template <class F> std::vector<float> minPhaseIR(F dbFn, double sr, size_t N = 8192, size_t L = 4096) {
  std::vector<double> re(N), im(N);
  for (size_t k = 0; k <= N / 2; k++) { double db = std::max(-100.0, dbFn(std::max(k * sr / N, 1.0))); re[k] = db / 20 * M_LN10; if (k > 0 && k < N / 2) re[N - k] = re[k]; }
  fft(re, im, true);
  for (size_t n = 1; n < N / 2; n++) { re[n] *= 2; im[n] = 0; }
  for (size_t n = N / 2 + 1; n < N; n++) { re[n] = 0; im[n] = 0; }
  im[0] = 0; im[N / 2] = 0;
  fft(re, im, false);
  for (size_t k = 0; k < N; k++) { double e = std::exp(re[k]), ph = im[k]; re[k] = e * std::cos(ph); im[k] = e * std::sin(ph); }
  fft(re, im, true);
  std::vector<float> h(L);
  for (size_t n = 0; n < L; n++) h[n] = (float)(re[n] * (n > L - 512 ? .5 * (1 + std::cos(M_PI * (double)(n - (L - 512)) / 512)) : 1));
  return h;
}

template <class V> void biqArr(V& x, double sr, int hp, double f0, double Q) {  // RBJ HP/LP over a whole buffer (linear Q)
  double w = 2 * M_PI * f0 / sr, cs = std::cos(w), al = std::sin(w) / (2 * Q), b0, b1, b2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
  if (hp) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; } else { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; }
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (auto& s : x) { double v = s, o = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = v; y2 = y1; y1 = o; s = (float)o; }
}

// mic: 0 dyn57, 1 rib121, 2 blend
inline std::vector<float> cabIR(const CabDef& c, int mic, double p, double sr) {
  Ripple rp(c.seed);
  auto db = [&](int m, double pp) { return [&, m, pp](double f) { return curveDB(c.pts, f) + rp(f) + micDB(m, f, pp); }; };
  std::vector<float> h;
  if (mic == 2) {
    auto a = minPhaseIR(db(0, p), sr), b = minPhaseIR(db(1, p * .8), sr);
    size_t d = (size_t)std::lround(.00003 * sr);  // phase-aligned blend
    h.resize(a.size()); for (size_t n = 0; n < a.size(); n++) h[n] = .5f * (a[n] + (n >= d ? b[n - d] : 0));
  } else h = minPhaseIR(db(mic, p), sr);
  size_t r = (size_t)std::lround(.0011 * sr * (1 + .3 * p));
  for (size_t n = h.size() - 1; n >= r; n--) h[n] += .1f * h[n - r];
  {  // dense HF tail (box reflections / cone breakup), follows the cab's own response
    Mulberry rnd(c.seed + 100); size_t n = h.size(), M = 2 * n;
    std::vector<double> er(M), ei(M), hr(M), hi(M);
    for (size_t i = 0; i < n; i++) { er[i] = (rnd() * 2 - 1) * std::exp(-(double)i / (c.tail0 * sr)) * std::min(1.0, i / (.0003 * sr)); hr[i] = h[i]; }
    fft(er, ei, false); fft(hr, hi, false);
    for (size_t k = 0; k < M; k++) { double a = er[k] * hr[k] - ei[k] * hi[k], b = er[k] * hi[k] + ei[k] * hr[k]; er[k] = a; ei[k] = b; }
    fft(er, ei, true);
    std::vector<float> t(n), hh(h);
    for (size_t i = 0; i < n; i++) t[i] = (float)er[i];
    biqArr(t, sr, 1, 1500, .7); biqArr(t, sr, 1, 1500, .7); biqArr(hh, sr, 1, 1500, .7);
    double eh = 0, et = 0; for (size_t i = 0; i < n; i++) { eh += (double)hh[i] * hh[i]; et += (double)t[i] * t[i]; }
    double g = c.tail1 * std::sqrt(eh / et) * (1 - .4 * p);
    for (size_t i = 0; i < n; i++) h[i] += (float)(g * t[i]);
  }
  double acc = 0; int cnt = 0;
  for (double f : {500.0, 700.0, 1000.0, 1400.0, 2000.0}) {
    double a = 0, b = 0, w = 2 * M_PI * f / sr;
    for (size_t n = 0; n < h.size(); n++) { a += h[n] * std::cos(w * n); b -= h[n] * std::sin(w * n); }
    acc += std::hypot(a, b); cnt++;
  }
  double g = cnt / acc; for (auto& v : h) v = (float)(v * g);
  return h;
}

}  // namespace gl
