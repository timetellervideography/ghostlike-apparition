// Ghostlike Apparition DSP engine. Port of the web preview's signal chain:
// input → gate → overdrive → tube amp (4x oversampled triode stages, passive tonestack, push-pull/SE power amp with sag)
// → cab IR (partitioned convolution) → 5-band EQ → ping-pong delay → reverb → output → limiter.
// All amp/cab data comes from engine.json in the content pack, so voicing updates need no new binary.
#include "gl_engine_api.h"
#include "cab_ir.h"
#include "json.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#ifndef GL_ENGINE_VERSION
#define GL_ENGINE_VERSION "dev"
#endif

namespace gl {

// ---------------------------------------------------------------- helpers
constexpr int OS = 4, T = 48, TP = T / OS;
static const std::array<double, T> H = [] {
  std::array<double, T> h{}; double fc = .45 / OS, s = 0;
  for (int i = 0; i < T; i++) { double n = i - (T - 1) / 2.0, w = .42 - .5 * std::cos(2 * M_PI * i / (T - 1)) + .08 * std::cos(4 * M_PI * i / (T - 1));
    h[i] = (n == 0 ? 2 * fc : std::sin(2 * M_PI * fc * n) / (M_PI * n)) * w; s += h[i]; }
  for (auto& v : h) v /= s; return h; }();
inline double lpA(double fc, double fs) { return std::exp(-2 * M_PI * fc / fs); }
inline double ft(double x) { return x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x); }
inline double logPot(double x) { return (std::exp(3.5 * x) - 1) / (std::exp(3.5) - 1); }
inline double dB(double x) { return std::pow(10.0, x / 20); }
template <class X> X clampv(X v, X a, X b) { return v < a ? a : v > b ? b : v; }

// RBJ biquad with linear Q (the worklet's rbj())
inline std::array<double, 5> rbj(const char* type, double f, double Q, double db, double fs) {
  double A = std::pow(10, db / 40), w = 2 * M_PI * f / fs, cs = std::cos(w), sn = std::sin(w), al = sn / (2 * Q), b0, b1, b2, a0, a1, a2;
  std::string t(type);
  if (t == "pk") { b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; }
  else if (t == "hs") { double sa = 2 * std::sqrt(A) * al; b0 = A * ((A + 1) + (A - 1) * cs + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sa); a0 = (A + 1) - (A - 1) * cs + sa; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - sa; }
  else if (t == "lp") { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
  else { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
  return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// Web Audio BiquadFilterNode semantics (LP/HP Q in dB, shelves with S = 1, peaking Q linear)
struct WBQ {
  enum { LP, HP, LS, HS, PK };
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void set(int type, double f, double Q, double g, double sr) {
    f = clampv(f, 1.0, sr * .499);
    double A = std::pow(10, g / 40), w = 2 * M_PI * f / sr, cs = std::cos(w), sn = std::sin(w), B0, B1, B2, A0, A1, A2;
    if (type == LP || type == HP) { double al = sn / (2 * std::pow(10, Q / 20));
      if (type == LP) { B0 = (1 - cs) / 2; B1 = 1 - cs; B2 = B0; } else { B0 = (1 + cs) / 2; B1 = -(1 + cs); B2 = B0; }
      A0 = 1 + al; A1 = -2 * cs; A2 = 1 - al; }
    else if (type == PK) { double al = sn / (2 * Q); B0 = 1 + al * A; B1 = -2 * cs; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * cs; A2 = 1 - al / A; }
    else { double al = sn / 2 * std::sqrt(2.0), s = 2 * al * std::sqrt(A);
      if (type == LS) { B0 = A * ((A + 1) - (A - 1) * cs + s); B1 = 2 * A * ((A - 1) - (A + 1) * cs); B2 = A * ((A + 1) - (A - 1) * cs - s); A0 = (A + 1) + (A - 1) * cs + s; A1 = -2 * ((A - 1) + (A + 1) * cs); A2 = (A + 1) + (A - 1) * cs - s; }
      else { B0 = A * ((A + 1) + (A - 1) * cs + s); B1 = -2 * A * ((A - 1) + (A + 1) * cs); B2 = A * ((A + 1) + (A - 1) * cs - s); A0 = (A + 1) - (A - 1) * cs + s; A1 = 2 * ((A - 1) - (A + 1) * cs); A2 = (A + 1) - (A - 1) * cs - s; } }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  inline double tick(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void clear() { z1 = z2 = 0; }
};

struct Smooth {  // AudioParam.setTargetAtTime
  double v = 0, t = 0, k = 1;
  void init(double val, double tc, double sr) { v = t = val; k = 1 - std::exp(-1 / (tc * sr)); }
  inline double tick() { v += k * (t - v); return v; }
};

// ---------------------------------------------------------------- config
struct Stage { double g, cf, k, cc, ml, A, B; };
struct AmpDef {
  double pre0, pre1, tight0, tight1, pa, sag, sagRel, otLp, lvl, paB, dp, pr, bias;
  bool se; int tsAt; std::string ts; bool hasVc = false; double vc[3]{};
  std::vector<Stage> stages; double mk = 0;
};
struct ParamDef { std::string id; double min = 0, max = 1, def = 0; std::vector<std::string> e; };

// ---------------------------------------------------------------- tonestack (Yeh & Smith), bilinear
using TSV = std::array<double, 7>;
static TSV tonestack(const TSV& c, double t, double m, double l, double fs) {
  double R1 = c[0], R2 = c[1], R3 = c[2], R4 = c[3], C1 = c[4], C2 = c[5], C3 = c[6];
  double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
  double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
  double b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4 + t * l * C1 * C2 * C3 * R1 * R2 * R4;
  double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
  double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4) + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
  double a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4) - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4) + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4) + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
  double cc = 2 * fs, c2 = cc * cc, c3 = c2 * cc;
  double B0 = -b1 * cc - b2 * c2 - b3 * c3, B1 = -b1 * cc + b2 * c2 + 3 * b3 * c3, B2 = b1 * cc + b2 * c2 - 3 * b3 * c3, B3 = b1 * cc - b2 * c2 + b3 * c3;
  double A0 = -1 - a1 * cc - a2 * c2 - a3 * c3, A1 = -3 - a1 * cc + a2 * c2 + 3 * a3 * c3, A2 = -3 + a1 * cc + a2 * c2 - 3 * a3 * c3, A3 = -1 + a1 * cc - a2 * c2 + a3 * c3;
  return {B0 / A0, B1 / A0, B2 / A0, B3 / A0, A1 / A0, A2 / A0, A3 / A0};
}
static double tsMag(const TSV& k, double f, double fs) {
  double w = 2 * M_PI * f / fs, nr = 0, ni = 0, dr = 1, di = 0;
  for (int i = 0; i < 4; i++) { nr += k[i] * std::cos(w * i); ni -= k[i] * std::sin(w * i); }
  for (int i = 1; i < 4; i++) { dr += k[3 + i] * std::cos(w * i); di -= k[3 + i] * std::sin(w * i); }
  return std::hypot(nr, ni) / std::hypot(dr, di);
}

// ---------------------------------------------------------------- tube amp (port of the gl-amp worklet)
constexpr int MAXST = 6;
struct AmpCh {
  double up[2 * TP], dn[2 * T]; int upI, dnI;
  double inLp, brt, env, w0, w1, w2, bqz[10], lc[MAXST], bi[MAXST], hp[MAXST], lp[MAXST];
  void clear() { memset(this, 0, sizeof(*this)); }
};
struct AmpCoef {
  double pre, inA, brtA, brtG, sg[MAXST * 8], bq[25], paG, lvl, sag, sagA, sagR, bC, bR, bAmt, paB, mk; TSV ts; int ns, tsAt; bool se;
};
static inline double ampStep(const AmpCoef& c, AmpCh& S, double x) {
  S.upI = S.upI == 0 ? TP - 1 : S.upI - 1; S.up[S.upI] = x; S.up[S.upI + TP] = x;
  for (int p = 0; p < OS; p++) {
    double v = 0; for (int k = 0; k < TP; k++) v += H[k * OS + p] * S.up[S.upI + k]; v *= OS;
    S.inLp = c.inA * S.inLp + (1 - c.inA) * v; v -= S.inLp; S.brt = c.brtA * S.brt + (1 - c.brtA) * v; v = (v + c.brtG * (v - S.brt)) * c.pre;
    for (int i = 0; i < c.ns; i++) {
      const double* s = c.sg + i * 8; const double cA = s[2], hA = s[3], mA = s[4], A = s[5], Bc = s[6];
      double lc = S.lc[i] = cA * S.lc[i] + (1 - cA) * v;
      double bias = S.bi[i]; const double u = s[0] * (s[1] * lc + (v - lc)) + bias;
      if (u > 0) bias -= c.bC * u * c.bAmt; bias -= c.bR * bias; S.bi[i] = bias < -4 ? -4 : bias;
      double q = u > 0 ? -A * ft(u / A) : -Bc * ft(u / Bc);
      const double hp = S.hp[i] = hA * S.hp[i] + (1 - hA) * q; q -= hp;
      v = S.lp[i] = mA * S.lp[i] + (1 - mA) * q;
      if (i == c.tsAt) { const auto& t = c.ts; double y0 = t[0] * v + S.w0; S.w0 = t[1] * v - t[4] * y0 + S.w1; S.w1 = t[2] * v - t[5] * y0 + S.w2; S.w2 = t[3] * v - t[6] * y0; v = y0 * c.mk; }
    }
    for (int j = 0; j < 3; j++) { const double* b = c.bq + j * 5; double* z = S.bqz + j * 2; double r = b[0] * v + z[0]; z[0] = b[1] * v - b[3] * r + z[1]; z[1] = b[2] * v - b[4] * r; v = r; }
    v *= c.paG; const double h = 1 - c.sag * (S.env < 1 ? S.env : 1);
    double o2 = c.se ? (v > 0 ? .85 * h * ft(v / (.85 * h)) : 1.25 * h * ft(v / (1.25 * h))) : h * (ft(v / h + c.paB) - ft(c.paB));
    const double ab = o2 < 0 ? -o2 : o2; S.env += (ab > S.env ? c.sagA : c.sagR) * (ab - S.env);
    for (int j = 3; j < 5; j++) { const double* b = c.bq + j * 5; double* z = S.bqz + j * 2; double r = b[0] * o2 + z[0]; z[0] = b[1] * o2 - b[3] * r + z[1]; z[1] = b[2] * o2 - b[4] * r; o2 = r; }
    S.dnI = S.dnI == 0 ? T - 1 : S.dnI - 1; S.dn[S.dnI] = o2 * c.lvl; S.dn[S.dnI + T] = o2 * c.lvl;
  }
  double d = 0; for (int i = 0; i < T; i++) d += H[i] * S.dn[S.dnI + i]; return d;
}

// 4x oversampled tanh shaper (WaveShaperNode with oversample '4x'); curve = tanh(12.8 x) clamped at |x| = 1
struct OSShaper {
  double up[2 * TP]{}, dn[2 * T]{}; int ui = 0, di = 0;
  inline double step(double x) {
    ui = ui == 0 ? TP - 1 : ui - 1; up[ui] = x; up[ui + TP] = x;
    for (int p = 0; p < OS; p++) {
      double v = 0; for (int k = 0; k < TP; k++) v += H[k * OS + p] * up[ui + k]; v *= OS;
      v = std::tanh(12.8 * clampv(v, -1.0, 1.0));
      di = di == 0 ? T - 1 : di - 1; dn[di] = v; dn[di + T] = v;
    }
    double d = 0; for (int i = 0; i < T; i++) d += H[i] * dn[di + i]; return d;
  }
};

// ---------------------------------------------------------------- partitioned convolution (P = 64, FFT 128, latency 64)
constexpr int CP = 64, CN = 128, CMAXP = 64;
using cpx = std::complex<float>;
struct FFT128 {
  std::array<cpx, CN / 2> tw; std::array<int, CN> rev;
  FFT128() { for (int i = 0; i < CN / 2; i++) tw[i] = std::polar(1.0f, (float)(-2 * M_PI * i / CN));
    for (int i = 0; i < CN; i++) { int r = 0; for (int b = 0; b < 7; b++) if (i & (1 << b)) r |= 1 << (6 - b); rev[i] = r; } }
  void run(cpx* a, bool inv) const {
    for (int i = 0; i < CN; i++) if (i < rev[i]) std::swap(a[i], a[rev[i]]);
    for (int len = 2; len <= CN; len <<= 1) { int st = CN / len;
      for (int i = 0; i < CN; i += len) for (int j = 0; j < len / 2; j++) { cpx w = tw[j * st]; if (inv) w = std::conj(w); cpx u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; } }
    if (inv) for (int i = 0; i < CN; i++) a[i] *= 1.0f / CN;
  }
};
static const FFT128 fft128;
struct Kernel { int parts = 0; std::array<std::array<cpx, CN>, CMAXP> H; };
static Kernel* makeKernel(const std::vector<float>& h) {
  auto* k = new Kernel(); k->parts = std::min(CMAXP, (int)((h.size() + CP - 1) / CP));
  for (int p = 0; p < k->parts; p++) { auto& a = k->H[p]; a.fill(0); for (int i = 0; i < CP && p * CP + i < (int)h.size(); i++) a[i] = h[p * CP + i]; fft128.run(a.data(), false); }
  return k;
}
struct ConvCh {
  std::array<float, CP> inb{}, outb{}; std::array<float, CP> prev{}; std::array<std::array<cpx, CN>, CMAXP> fdl{}; int pos = 0, fill = 0;
  inline float step(float x, const Kernel* k) {
    float y = outb[fill]; inb[fill] = x;
    if (++fill == CP) {
      fill = 0; auto& X = fdl[pos];
      for (int i = 0; i < CP; i++) { X[i] = prev[i]; X[i + CP] = inb[i]; }
      prev = inb; fft128.run(X.data(), false);
      std::array<cpx, CN> Y; Y.fill(0);
      for (int p = 0; p < k->parts; p++) { const auto& Xp = fdl[(pos - p + CMAXP) % CMAXP]; const auto& Hp = k->H[p]; for (int i = 0; i < CN; i++) Y[i] += Xp[i] * Hp[i]; }
      fft128.run(Y.data(), true);
      for (int i = 0; i < CP; i++) outb[i] = Y[i + CP].real();
      pos = (pos + 1) % CMAXP;
    }
    return y;
  }
};

// ---------------------------------------------------------------- gate (port of gl-gate)
struct GateCh { double e = 0, g = 1; int op = 1, hd = 0; };

// ---------------------------------------------------------------- per-channel mono chain
struct Chain {
  GateCh gate; WBQ odHp, odTone; OSShaper odWs; std::array<float, 1024> odDl{}; int odDw = 0; AmpCh amp; ConvCh conv; WBQ cabLo, cabHi; WBQ eq[5];
};

// ---------------------------------------------------------------- reverb (8-line FDN with input diffusion)
struct Reverb {
  static constexpr int NL = 8;
  std::vector<float> line[NL]; int len[NL]{}, wp[NL]{}; double lp[NL]{}, g[NL]{}, damp = .5;
  std::vector<float> ap[4]; int apLen[4]{}, apW[4]{};
  std::vector<float> pre; int preW = 0, preN = 0; double sr = 48000;
  void init(double s) {
    sr = s; const double ms[NL] = {31.7, 37.3, 41.9, 47.3, 53.1, 59.3, 67.7, 73.1}, apms[4] = {4.7, 3.6, 12.7, 9.3};
    for (int i = 0; i < NL; i++) { len[i] = (int)(ms[i] * .001 * sr); line[i].assign(len[i] + 1, 0); }
    for (int i = 0; i < 4; i++) { apLen[i] = (int)(apms[i] * .001 * sr); ap[i].assign(apLen[i] + 1, 0); }
    pre.assign((size_t)(sr * .13) + 2, 0);
  }
  void set(double decay, double tone, double preMs) {
    for (int i = 0; i < NL; i++) g[i] = std::pow(10.0, -3.0 * len[i] / (decay * sr));
    double fc = 800 * std::pow(12.0, tone / 11); damp = 1 - std::exp(-2 * M_PI * fc / sr);
    preN = clampv((int)(preMs * .001 * sr), 0, (int)pre.size() - 2);
  }
  void clear() { for (auto& l : line) std::fill(l.begin(), l.end(), 0.f); for (auto& a : ap) std::fill(a.begin(), a.end(), 0.f); std::fill(pre.begin(), pre.end(), 0.f); for (auto& v : lp) v = 0; }
  inline void step(double inL, double inR, double& oL, double& oR) {
    pre[preW] = (float)(.5 * (inL + inR)); int rp = preW - preN; if (rp < 0) rp += (int)pre.size(); double x = pre[rp]; if (++preW == (int)pre.size()) preW = 0;
    double sideL = inL - inR;  // keep some stereo image in the input
    for (int i = 0; i < 4; i++) { int r = apW[i]; double d = ap[i][r]; double v = x + .6 * d; ap[i][r] = (float)v; x = d - .6 * v; if (++apW[i] > apLen[i] - 1) apW[i] = 0; }
    double o[NL];
    for (int i = 0; i < NL; i++) { int r = wp[i] + 1; if (r > len[i]) r = 0; o[i] = line[i][r]; lp[i] += damp * (o[i] - lp[i]); }
    // Hadamard 8x8 (fast Walsh-Hadamard), normalised
    double h[NL]; for (int i = 0; i < NL; i++) h[i] = lp[i];
    for (int s = 1; s < NL; s <<= 1) for (int i = 0; i < NL; i += s * 2) for (int j = i; j < i + s; j++) { double a = h[j], b = h[j + s]; h[j] = a + b; h[j + s] = a - b; }
    const double nrm = 1 / std::sqrt((double)NL);
    for (int i = 0; i < NL; i++) { double in = x * .35 + sideL * (i & 1 ? -.08 : .08); line[i][wp[i]] = (float)(h[i] * nrm * g[i] + in); if (++wp[i] > len[i]) wp[i] = 0; }
    oL = (lp[0] + lp[2] + lp[4] + lp[6]) * .5; oR = (lp[1] + lp[3] + lp[5] + lp[7]) * .5;
  }
};

// ---------------------------------------------------------------- engine
enum Pid { inGain, outGain, gateOn, gateThr, gateRel, gateHold, gateRange, odOn, odDrive, odTone, odLevel, amp, gain, bass, mid, treble, presence, depth, master,
  ampTight, ampBright, power, cabOn, cabType, mic, micPos, cabLo, cabHi, eqOn, eq1, eq2, eq3, eq4, eq5, dlyOn, dlyTime, dlyFb, dlyMix, dlyTone,
  revOn, revDecay, revPre, revMix, revTone, NP };
static const char* PNAMES[NP] = {"inGain", "outGain", "gateOn", "gateThr", "gateRel", "gateHold", "gateRange", "odOn", "odDrive", "odTone", "odLevel", "amp", "gain", "bass", "mid", "treble", "presence", "depth", "master",
  "ampTight", "ampBright", "power", "cabOn", "cabType", "mic", "micPos", "cabLo", "cabHi", "eqOn", "eq1", "eq2", "eq3", "eq4", "eq5", "dlyOn", "dlyTime", "dlyFb", "dlyMix", "dlyTone",
  "revOn", "revDecay", "revPre", "revMix", "revTone"};

struct Engine {
  double sr; int maxBlock;
  std::vector<ParamDef> pdef; std::array<std::atomic<double>, NP> want; std::array<double, NP> cur;
  std::vector<std::string> ampKeys, cabKeys, micKeys; std::vector<AmpDef> amps; std::vector<CabDef> cabs; std::map<std::string, TSV> tsTab; double eqf[5] = {80, 240, 750, 2200, 6600};

  Chain ch[2]; bool prevSame = true;
  AmpCoef ac{}; int ampCur = 0, ampPending = -1; double fade = 1;
  Smooth sIn, sOut, sOdW, sOdD, sOdDrv, sOdLvl, sStby, sCabW, sCabD, sEqW, sEqD, sDWet, sFb, sDt, sRWet;
  double gate[6]{};
  std::vector<float> dL, dR; int dW = 0; WBQ lpL, lpR; double lpRout = 0;
  Reverb rev; double revK = 1.0; int odDelay = 0;
  double limEnv = 0, limGain = 1, limAtk, limRel, limMake;

  // cab kernels: built on a worker thread, swapped in at block start, retired back to the worker
  Kernel* active = nullptr; std::atomic<Kernel*> pending{nullptr};
  std::array<std::atomic<Kernel*>, 16> retired{}; int retW = 0;
  std::thread worker; std::mutex mx; std::condition_variable cv; bool quit = false; int reqGen = 0; std::chrono::steady_clock::time_point reqAt;

  // meters + spectrum tap
  std::atomic<float> mInPk{0}, mInRms{0}, mOutPk{0}, mOutRms{0}, mPrePk{0}; std::atomic<int> mGate{1};
  std::vector<float> tap; std::atomic<int> tapW{0};

  int pidx(const std::string& id) const { for (int i = 0; i < NP; i++) if (id == PNAMES[i]) return i; return -1; }
  const ParamDef* pd(int i) const { for (auto& p : pdef) if (p.id == PNAMES[i]) return &p; return nullptr; }

  bool load(const char* js) {
    Json j; if (!js || !Json::parse(js, j)) return false;
    for (auto& [id, p] : j["params"].obj) { ParamDef d; d.id = id; d.min = p["min"].d(0); d.max = p["max"].d(1);
      if (p.has("e")) { for (auto& e : p["e"].arr) d.e.push_back(e.str); auto it = std::find(d.e.begin(), d.e.end(), p["def"].str); d.def = it == d.e.end() ? 0 : it - d.e.begin(); d.min = 0; d.max = (double)d.e.size() - 1; }
      else d.def = p["def"].d(0);
      pdef.push_back(d); }
    for (auto& [k, t] : j["ts"].obj) { TSV v{}; for (int i = 0; i < 7; i++) v[i] = t[i].d(); tsTab[k] = v; }
    const ParamDef* pa = nullptr; for (auto& p : pdef) if (p.id == "amp") pa = &p;
    if (!pa) return false; ampKeys = pa->e;
    for (auto& key : ampKeys) {
      const Json& a = j["amps"][key]; if (a.type != Json::Obj) return false;
      AmpDef d; d.pre0 = a["pre"][0].d(); d.pre1 = a["pre"][1].d(); d.tight0 = a["tight"][0].d(); d.tight1 = a["tight"][1].d();
      d.pa = a["pa"].d(1); d.sag = a["sag"].d(.1); d.sagRel = a["sagRel"].d(.15); d.otLp = a["otLp"].d(9000); d.lvl = a["lvl"].d(.3);
      d.paB = a["paB"].d(0); d.dp = a["dp"].d(90); d.pr = a["pr"].d(3200); d.bias = a["bias"].d(.6); d.se = a["paType"].str == "se"; d.ts = a["ts"].str;
      if (a.has("vc")) { d.hasVc = true; for (int i = 0; i < 3; i++) d.vc[i] = a["vc"][i].d(); }
      for (auto& s : a["stages"].arr) d.stages.push_back({s["g"].d(), s["cf"].d(), s["k"].d(), s["cc"].d(), s["ml"].d(), s["A"].d(.9), s["B"].d(1.6)});
      if (d.stages.empty() || d.stages.size() > MAXST || !tsTab.count(d.ts)) return false;
      d.tsAt = a.has("tsAt") ? (int)a["tsAt"].d() : (int)d.stages.size() - 1;
      amps.push_back(d); }
    for (auto& p : pdef) if (p.id == "cabType") cabKeys = p.e; else if (p.id == "mic") micKeys = p.e;
    for (auto& key : cabKeys) { const Json& c = j["cabs"][key]; CabDef d; d.seed = (uint32_t)c["seed"].d(3); d.tail0 = c["tail"][0].d(.012); d.tail1 = c["tail"][1].d(2.5);
      for (auto& pt : c["pts"].arr) d.pts.push_back({pt[0].d(), pt[1].d()}); if (d.pts.size() < 2) return false; cabs.push_back(d); }
    if (j.has("eqf")) for (int i = 0; i < 5; i++) eqf[i] = j["eqf"][i].d(eqf[i]);
    if (j.has("revK")) revK = j["revK"].d(1);
    odDelay = clampv((int)std::lround(j["odLatency"].d(0) * sr) - 12, 0, 1000);  // minus our own shaper FIR delay
    for (int i = 0; i < NP; i++) { const ParamDef* d = pd(i); double v = d ? d->def : 0; want[i] = v; cur[i] = v; }
    return true;
  }

  // ------------------------------------------------------------ amp coefficients (worklet coeffs())
  void ampCoeffs() {
    AmpDef& a = amps[ampCur]; const double fs = sr * OS; auto& p = cur;
    ac.pre = a.pre0 * std::pow(a.pre1 / a.pre0, p[gain] / 11);
    ac.inA = lpA(p[ampTight] > .5 ? a.tight1 : a.tight0, fs); ac.brtA = lpA(1500, fs); ac.brtG = p[ampBright] > .5 ? 1.2 : 0;
    ac.ns = (int)a.stages.size();
    for (int i = 0; i < ac.ns; i++) { const Stage& s = a.stages[i]; double* o = ac.sg + i * 8;
      o[0] = s.g; o[1] = s.k * (p[ampTight] > .5 && i < 2 ? .75 : 1); o[2] = lpA(s.cf, fs); o[3] = lpA(s.cc, fs); o[4] = lpA(s.ml, fs); o[5] = s.A; o[6] = s.B; }
    double t = std::min(1.0, std::max(.002, p[treble] / 11)), m = std::min(1.0, std::max(.002, p[mid] / 11)), l = std::max(.002, logPot(p[bass] / 11));
    const TSV& tv = tsTab[a.ts]; ac.ts = tonestack(tv, t, m, l, fs);
    if (a.mk == 0) a.mk = 1 / tsMag(tonestack(tv, .5, .5, logPot(.5), fs), 700, fs); ac.mk = a.mk;
    auto g = [](double v) { return (v - 5.5) / 5.5; };
    std::array<double, 5> b[5] = {rbj("pk", a.dp, .8, g(p[depth]) * 9, fs), rbj("hs", a.pr, .7, g(p[presence]) * 8, fs),
      a.hasVc ? rbj("pk", a.vc[0], a.vc[1], a.vc[2], fs) : std::array<double, 5>{1, 0, 0, 0, 0}, rbj("hp", 40, .6, 0, fs), rbj("lp", a.otLp, .6, 0, fs)};
    for (int i = 0; i < 5; i++) for (int k = 0; k < 5; k++) ac.bq[i * 5 + k] = b[i][k];
    ac.tsAt = a.tsAt; ac.paB = a.paB;
    ac.paG = a.pa * (.5 + 1.2 * p[master] / 11); ac.lvl = a.lvl * std::pow(p[master] / 6.6, 1.5); ac.sag = a.sag; ac.se = a.se;
    ac.sagA = 1 - std::exp(-1 / (.01 * fs)); ac.sagR = 1 - std::exp(-1 / (a.sagRel * fs)); ac.bC = 1 - std::exp(-1 / (.0005 * fs)); ac.bR = 1 - std::exp(-1 / (.05 * fs)); ac.bAmt = a.bias;
  }

  // ------------------------------------------------------------ cab worker
  void requestCab() { { std::lock_guard<std::mutex> l(mx); reqGen++; reqAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(40); } cv.notify_one(); }
  Kernel* buildCab() { int c = clampv((int)cur[cabType], 0, (int)cabs.size() - 1), m = clampv((int)cur[mic], 0, 2); return makeKernel(cabIR(cabs[c], m, cur[micPos], sr)); }
  std::array<double, 3> cabWant() { return {want[cabType].load(), want[mic].load(), want[micPos].load()}; }
  void workerLoop(int done) {  // done = request generation already covered by the kernel built in init()
    std::unique_lock<std::mutex> l(mx);
    while (!quit) {
      for (auto& r : retired) if (Kernel* k = r.exchange(nullptr)) delete k;
      if (reqGen != done) {
        if (std::chrono::steady_clock::now() < reqAt) { cv.wait_until(l, reqAt); continue; }  // debounce knob drags
        done = reqGen; auto w = cabWant(); l.unlock();
        int c = clampv((int)w[0], 0, (int)cabs.size() - 1), m = clampv((int)w[1], 0, 2);
        Kernel* k = makeKernel(cabIR(cabs[c], m, w[2], sr));
        if (Kernel* old = pending.exchange(k)) delete old;
        l.lock(); continue;
      }
      cv.wait_for(l, std::chrono::milliseconds(200));
    }
  }

  // ------------------------------------------------------------ parameter application (block start)
  void apply(int i) {
    auto& p = cur; double v = p[i];
    switch (i) {
      case inGain: sIn.t = dB(v); break;
      case outGain: sOut.t = dB(v); break;
      case odOn: sOdW.t = v > .5; sOdD.t = v > .5 ? 0 : 1; break;
      case odDrive: sOdDrv.t = 4 * std::pow(30.0, v / 11) / 8; break;
      case odTone: for (auto& c : ch) c.odTone.set(WBQ::LP, 900 * std::pow(6.5, v / 11), .6, 0, sr); break;
      case odLevel: sOdLvl.t = std::pow(v / 11, 2) * 1.6; break;
      case amp: { int a = clampv((int)std::lround(v), 0, (int)amps.size() - 1); if (a != ampCur) ampPending = a; else ampPending = -1; } break;
      case gain: case bass: case mid: case treble: case presence: case depth: case master: case ampTight: case ampBright: ampCoeffs(); break;
      case power: sStby.t = v > .5; break;
      case cabOn: sCabW.t = v > .5; sCabD.t = v > .5 ? 0 : 1; break;
      case cabType: case mic: case micPos: requestCab(); break;
      case cabLo: for (auto& c : ch) c.cabLo.set(WBQ::HP, v, .7, 0, sr); break;
      case cabHi: for (auto& c : ch) c.cabHi.set(WBQ::LP, v, .7, 0, sr); break;
      case eqOn: sEqW.t = v > .5; sEqD.t = v > .5 ? 0 : 1; break;
      case eq1: case eq2: case eq3: case eq4: case eq5: { int k = i - eq1; int t = k == 0 ? WBQ::LS : k == 4 ? WBQ::HS : WBQ::PK; for (auto& c : ch) c.eq[k].set(t, eqf[k], 1.1, v, sr); } break;
      case dlyOn: case dlyMix: sDWet.t = p[dlyOn] > .5 ? p[dlyMix] / 100 * 1.4 : 0; break;
      case dlyTime: sDt.t = clampv(v / 1000 * sr, 1.0, (double)dL.size() - 4); break;
      case dlyFb: sFb.t = v / 100; break;
      case dlyTone: { double f = 1200 * std::pow(8.0, v / 11); lpL.set(WBQ::LP, f, .5, 0, sr); lpR.set(WBQ::LP, f, .5, 0, sr); } break;
      case revOn: case revMix: sRWet.t = p[revOn] > .5 ? p[revMix] / 100 * 1.6 * revK : 0; break;
      case revDecay: case revTone: case revPre: rev.set(p[revDecay], p[revTone], p[revPre]); break;
      default: break;  // gate params are read per block
    }
  }

  bool init(double s, int mb, const char* js) {
    sr = s; maxBlock = mb; if (!load(js)) return false;
    for (auto& c : ch) { c.amp.clear(); c.odHp.set(WBQ::HP, 720, .7, 0, sr); }
    double tc = .015;
    for (Smooth* m : {&sIn, &sOut, &sOdW, &sOdD, &sOdDrv, &sOdLvl, &sCabW, &sCabD, &sEqW, &sEqD, &sDWet, &sFb, &sRWet}) m->init(0, tc, sr);
    sStby.init(0, .08, sr); sDt.init(1, .06, sr);
    dL.assign((size_t)(sr * 2.05) + 8, 0); dR.assign(dL.size(), 0);
    rev.init(sr);
    limAtk = 1 - std::exp(-1 / (.002 * sr)); limRel = 1 - std::exp(-1 / (.1 * sr)); limMake = dB(.57);
    tap.assign(8192, 0);
    ampCur = clampv((int)cur[amp], 0, (int)amps.size() - 1);
    for (int i = 0; i < NP; i++) if (i != amp) apply(i);
    ampCoeffs();
    for (Smooth* m : {&sIn, &sOut, &sOdW, &sOdD, &sOdDrv, &sOdLvl, &sStby, &sCabW, &sCabD, &sEqW, &sEqD, &sDWet, &sFb, &sDt, &sRWet}) m->v = m->t;
    active = buildCab();
    worker = std::thread([this, d = reqGen] { workerLoop(d); });
    return true;
  }
  ~Engine() { { std::lock_guard<std::mutex> l(mx); quit = true; } cv.notify_all(); if (worker.joinable()) worker.join();
    delete active; delete pending.exchange(nullptr); for (auto& r : retired) delete r.exchange(nullptr); }

  void resetState() { for (auto& c : ch) { c.amp.clear(); c.gate = GateCh(); c.conv = ConvCh(); } rev.clear(); std::fill(dL.begin(), dL.end(), 0.f); std::fill(dR.begin(), dR.end(), 0.f); }

  // ------------------------------------------------------------ mono chain, one sample
  inline double chainStep(Chain& c, double x, double gThr2, double gCl2, bool gOn, double gFl, double kE, double aG, double rG, int hs, double fa) {
    // gate
    GateCh& g = c.gate; g.e += kE * (x * x - g.e);
    if (g.e > gThr2) { g.op = 1; g.hd = hs; } else if (g.hd > 0) g.hd--; else if (g.e < gCl2) g.op = 0;
    double t = (!gOn || g.op) ? 1 : gFl; g.g = t > g.g ? aG * g.g + (1 - aG) * t : rG * g.g + (1 - rG) * t; x *= g.g;
    // overdrive
    // the preview's WaveShaper path lags the clean path by Chrome's oversampler latency; odDelay reproduces that (engine.json)
    double sh = c.odWs.step(c.odHp.tick(x) * sOdDrv.v); c.odDl[c.odDw] = (float)sh; sh = c.odDl[(c.odDw - odDelay) & 1023]; c.odDw = (c.odDw + 1) & 1023;
    double od = x + sh; od = c.odTone.tick(od) * sOdLvl.v; x = sOdW.v * od + sOdD.v * x;
    // amp
    x = ampStep(ac, c.amp, x) * fa * sStby.v;
    // cab
    double cb = c.conv.step((float)x, active); cb = c.cabHi.tick(c.cabLo.tick(cb)); x = sCabW.v * cb + sCabD.v * x;
    // eq
    double e = x; for (auto& q : c.eq) e = q.tick(e); x = sEqW.v * e + sEqD.v * x;
    return x;
  }

  void process(float* const* io, int nch, int n) {
    // swap in a freshly built cab kernel; the old one goes back to the worker (only this thread fills retired slots)
    if (pending.load(std::memory_order_acquire)) for (int i = 0; i < 16; i++) { int s = (retW + i) % 16;
      if (!retired[s].load()) { if (Kernel* k = pending.exchange(nullptr)) { retired[s].store(active); active = k; retW = s + 1; } break; } }
    for (int i = 0; i < NP; i++) { double v = want[i].load(std::memory_order_relaxed); if (v != cur[i]) { cur[i] = v; apply(i); } }
    auto& p = cur;
    const double gThr2 = std::pow(10.0, p[gateThr] / 10), gCl2 = gThr2 * .4, gFl = p[gateRange] <= -79.5 ? 0 : dB(p[gateRange]);
    const bool gOn = p[gateOn] > .5; const double kE = 1 - std::exp(-1 / (.003 * sr)), aG = std::exp(-1 / (.0005 * sr)), rG = std::exp(-1 / (std::max(1.0, p[gateRel]) * .001 * sr));
    const int hs = (int)std::lround(p[gateHold] * .001 * sr);
    float* L = io[0]; float* R = nch > 1 ? io[1] : io[0];
    bool same = nch < 2; if (!same) { same = true; for (int i = 0; i < n; i++) if (L[i] != R[i]) { same = false; break; } }
    if (!same && prevSame) ch[1] = ch[0];
    prevSame = same;
    double inPk = 0, inSq = 0, outPk = 0, outSq = 0, prePk = 0; const int DS = (int)dL.size(); int tw = tapW.load(std::memory_order_relaxed);
    for (int i = 0; i < n; i++) {
      for (Smooth* m : {&sIn, &sOut, &sOdW, &sOdD, &sOdDrv, &sOdLvl, &sStby, &sCabW, &sCabD, &sEqW, &sEqD, &sDWet, &sFb, &sDt, &sRWet}) m->tick();
      // amp model switch: fade out, reset, fade in (as in the worklet)
      double tgt = ampPending >= 0 ? 0 : 1; fade += tgt > fade ? std::min(1.0 / 256, tgt - fade) : std::max(-1.0 / 128, tgt - fade);
      if (ampPending >= 0 && fade <= 0) { ampCur = ampPending; ampPending = -1; ch[0].amp.clear(); ch[1].amp.clear(); ampCoeffs(); }
      double xl = L[i] * sIn.v, xr = R[i] * sIn.v;
      inPk = std::max(inPk, std::max(std::fabs(xl), std::fabs(xr))); inSq += xl * xl;
      double yl = chainStep(ch[0], xl, gThr2, gCl2, gOn, gFl, kE, aG, rG, hs, fade);
      double yr = same ? yl : chainStep(ch[1], xr, gThr2, gCl2, gOn, gFl, kE, aG, rG, hs, fade);
      // ping-pong delay
      double mono = .5 * (yl + yr), dt = sDt.v; int ip = (int)dt; double fr = dt - ip;
      auto rd = [&](const std::vector<float>& b) { int a = dW - ip; if (a < 0) a += DS; int c = a - 1; if (c < 0) c += DS; return b[a] * (1 - fr) + b[c] * fr; };
      double lpLout = lpL.tick(rd(dL)), dRout = rd(dR);
      dL[dW] = (float)(mono + sFb.v * lpRout); dR[dW] = (float)(sFb.v * lpLout); lpRout = lpR.tick(dRout); if (++dW == DS) dW = 0;
      double rl = yl + lpLout * sDWet.v, rr = yr + lpRout * sDWet.v;
      // reverb
      double wl, wr; rev.step(rl, rr, wl, wr); double ml = rl + wl * sRWet.v, mr = rr + wr * sRWet.v;
      ml *= sOut.v; mr *= sOut.v; prePk = std::max(prePk, std::max(std::fabs(ml), std::fabs(mr)));
      // limiter (DynamicsCompressor: -1 dB, 20:1, hard knee, 2 ms / 100 ms, auto make-up)
      double pk = std::max(std::fabs(ml), std::fabs(mr)), lv = pk > 1e-9 ? 20 * std::log10(pk) : -180, over = lv + 1, grDb = over > 0 ? -over * (1 - 1.0 / 20) : 0, gt = dB(grDb);
      limGain += (gt < limGain ? limAtk : limRel) * (gt - limGain); ml *= limGain * limMake; mr *= limGain * limMake;
      outPk = std::max(outPk, std::max(std::fabs(ml), std::fabs(mr))); outSq += ml * ml;
      if (nch > 1) { L[i] = (float)ml; R[i] = (float)mr; } else L[i] = (float)(.5 * (ml + mr));
      tap[tw] = (float)ml; tw = (tw + 1) & 8191;
    }
    tapW.store(tw, std::memory_order_release);
    auto hold = [](std::atomic<float>& a, double v) { float o = a.load(std::memory_order_relaxed); if (v > o) a.store((float)v, std::memory_order_relaxed); };
    hold(mInPk, inPk); hold(mOutPk, outPk); hold(mPrePk, prePk);
    mInRms.store((float)std::sqrt(inSq / std::max(1, n))); mOutRms.store((float)std::sqrt(outSq / std::max(1, n)));
    mGate.store(gOn ? ch[0].gate.op : 2);
  }

  int spectrum(const float* fq, float* out, int cnt) {
    const int N = 8192; std::vector<double> re(N), im(N); int w = tapW.load(std::memory_order_acquire);
    for (int i = 0; i < N; i++) re[i] = tap[(w + i) & 8191] * (.5 - .5 * std::cos(2 * M_PI * i / N));
    fft(re, im, false);
    for (int k = 0; k < cnt; k++) { double b = fq[k] * N / sr; int i = clampv((int)b, 1, N / 2 - 2); double f = b - i;
      double m0 = std::hypot(re[i], im[i]), m1 = std::hypot(re[i + 1], im[i + 1]), m = (m0 * (1 - f) + m1 * f) * 2 / (N * .5);
      out[k] = (float)(20 * std::log10(m + 1e-12)); }
    return cnt;
  }
};

}  // namespace gl

// ---------------------------------------------------------------- C ABI
using gl::Engine;
struct GLEngine { Engine e; };
static GLEngine* c_create(double sr, int mb, const char* js) { auto* g = new GLEngine(); if (!g->e.init(sr, mb, js)) { delete g; return nullptr; } return g; }
static void c_destroy(GLEngine* g) { delete g; }
static void c_set(GLEngine* g, const char* id, double v) { int i = g->e.pidx(id); if (i >= 0) g->e.want[i].store(v); }
static int c_choice(GLEngine* g, const char* id, const char* v) {
  int i = g->e.pidx(id); if (i < 0) return 0; const gl::ParamDef* d = g->e.pd(i); if (!d) return 0;
  for (size_t k = 0; k < d->e.size(); k++) if (d->e[k] == v) { g->e.want[i].store((double)k); return 1; } return 0; }
static double c_get(GLEngine* g, const char* id) { int i = g->e.pidx(id); return i >= 0 ? g->e.want[i].load() : 0; }
static void c_process(GLEngine* g, float* const* io, int nch, int n) { g->e.process(io, nch, n); }
static void c_reset(GLEngine* g) { g->e.resetState(); }
static int c_latency(GLEngine*) { return gl::CP; }
static int c_meters(GLEngine* g, float* o, int m) {
  float v[7] = {g->e.mInPk.exchange(0), g->e.mInRms.load(), g->e.mOutPk.exchange(0), g->e.mOutRms.load(), g->e.mPrePk.exchange(0), (float)g->e.mGate.load(), 0};
  int n = std::min(7, m); for (int i = 0; i < n; i++) o[i] = v[i]; return n; }
static int c_spectrum(GLEngine* g, const float* f, float* o, int n) { return g->e.spectrum(f, o, n); }

static const GLEngineAPI kAPI = {GL_ENGINE_ABI, GL_ENGINE_VERSION, c_create, c_destroy, c_set, c_choice, c_get, c_process, c_reset, c_latency, c_meters, c_spectrum};
extern "C" __attribute__((visibility("default"))) const GLEngineAPI* gl_engine_api(void) { return &kAPI; }
