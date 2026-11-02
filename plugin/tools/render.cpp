// Offline test harness: runs presets through the C++ engine with the band's DI and prints level + spectral deviation
// from the reference, using the same analysis as the web preview's __fit(). Optionally writes the rendered WAV.
// usage: render <engine.json> <presets.json> <di.wav> [presetIndex|all] [out.wav]
#include "../engine/engine.cpp"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::string slurp(const char* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

static std::vector<float> readWav(const char* p, int& sr) {
  std::string d = slurp(p); std::vector<float> out; size_t i = 12; int ch = 1, bits = 16, fmt = 1;
  while (i + 8 <= d.size()) {
    std::string id = d.substr(i, 4); uint32_t n; memcpy(&n, &d[i + 4], 4);
    if (id == "fmt ") { uint16_t f, c, b; uint32_t s; memcpy(&f, &d[i + 8], 2); memcpy(&c, &d[i + 10], 2); memcpy(&s, &d[i + 12], 4); memcpy(&b, &d[i + 22], 2); fmt = f; ch = c; sr = (int)s; bits = b; }
    if (id == "data") { size_t bps = bits / 8, frames = n / (bps * ch);
      for (size_t k = 0; k < frames; k++) { const char* q = &d[i + 8 + k * bps * ch]; float v;
        if (fmt == 3) memcpy(&v, q, 4); else if (bits == 16) { int16_t s; memcpy(&s, q, 2); v = s / 32768.f; } else { int32_t s = (uint8_t)q[0] << 8 | (uint8_t)q[1] << 16 | q[2] << 24; v = s / 2147483648.f; }
        out.push_back(v); } break; }
    i += 8 + n + (n & 1);
  }
  return out;
}
static void writeWav(const char* p, const std::vector<float>& L, const std::vector<float>& R, int sr) {
  std::ofstream f(p, std::ios::binary); uint32_t n = (uint32_t)L.size(), data = n * 4; auto w32 = [&](uint32_t v) { f.write((char*)&v, 4); }; auto w16 = [&](uint16_t v) { f.write((char*)&v, 2); };
  f.write("RIFF", 4); w32(36 + data); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16); f.write("data", 4); w32(data);
  for (uint32_t i = 0; i < n; i++) for (float v : {L[i], R[i]}) { int16_t s = (int16_t)std::lround(std::max(-1.f, std::min(1.f, v)) * 32767); f.write((char*)&s, 2); }
}

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: render engine.json presets.json di.wav [idx|all] [out.wav]\n"); return 1; }
  std::string cfg = slurp(argv[1]); gl::Json pj; if (!gl::Json::parse(slurp(argv[2]).c_str(), pj)) { fprintf(stderr, "bad presets\n"); return 1; }
  int sr = 48000; auto di = readWav(argv[3], sr);
  const GLEngineAPI* api = gl_engine_api();
  std::vector<int> idx; std::string which = argc > 4 ? argv[4] : "all";
  for (size_t i = 0; i < pj["presets"].size(); i++) if (which == "all" ? pj["presets"][i]["loop"].str == "ghost" : (int)i == atoi(which.c_str())) idx.push_back((int)i);
  const auto& ref = pj["ref"];
  for (int pi : idx) {
    const auto& pr = pj["presets"][pi];
    auto t0 = std::chrono::steady_clock::now();
    GLEngine* e = api->create(sr, 128, cfg.c_str()); if (!e) { fprintf(stderr, "engine config rejected\n"); return 1; }
    for (auto& [k, v] : pr["p"].obj) { if (v.type == gl::Json::Str) api->setChoice(e, k.c_str(), v.str.c_str()); else api->setParam(e, k.c_str(), v.d()); }
    {  // let the asynchronous cab rebuild land before rendering (in realtime this takes ~50 ms)
      std::vector<float> z(128); float* io[2] = {z.data(), z.data()}; api->process(e, io, 2, 128);
      std::this_thread::sleep_for(std::chrono::milliseconds(250)); api->process(e, io, 2, 128); api->reset(e); }
    if (getenv("GL_DEBUG")) { auto& E = ((GLEngine*)e)->e; gl::Kernel* k = E.buildCab(); double d = 0; for (int p = 0; p < k->parts; p++) for (int i = 0; i < gl::CN; i++) d += std::abs(k->H[p][i] - E.active->H[p][i]);
      fprintf(stderr, "cab %g mic %g pos %g  kernel diff %g\n", E.cur[gl::cabType], E.cur[gl::mic], E.cur[gl::micPos], d); delete k; }
    const int N = sr * 12; std::vector<float> L(N), R(N);
    for (int i = 0; i < N; i++) L[i] = R[i] = di[i % di.size()];
    for (int s = 0; s < N; s += 128) { float* io[2] = {&L[s], &R[s]}; api->process(e, io, 2, std::min(128, N - s)); }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    api->destroy(e);
    // analysis (bandsOf + __fit)
    const int F = 8192; std::vector<double> acc(F / 2); int cnt = 0;
    for (int s = 0; s + F <= N; s += F / 2) { std::vector<double> re(F), im(F); for (int i = 0; i < F; i++) re[i] = L[s + i] * (.5 - .5 * std::cos(2 * M_PI * i / F)); gl::fft(re, im, false); for (int k = 0; k < F / 2; k++) acc[k] += re[k] * re[k] + im[k] * im[k]; cnt++; }
    auto band = [&](double f) { double e = 0; for (int k = (int)std::ceil(f / std::pow(2, 1 / 6.) * F / sr); k <= f * std::pow(2, 1 / 6.) * F / sr; k++) e += acc[k]; return 10 * std::log10(e / cnt + 1e-20); };
    std::vector<std::pair<double, double>> diff; double m0 = 0; int mc = 0;
    for (size_t j = 0; j < ref.size(); j++) { double f = ref[j][0].d(), d = band(f) - ref[j][1].d(); diff.push_back({f, d}); if (f >= 403 && f <= 2560) { m0 += d; mc++; } }
    m0 /= mc; double G[5][2] = {{50, 101}, {160, 320}, {508, 1016}, {1613, 3225}, {5120, 8127}};
    double ss = 0; for (float v : L) ss += (double)v * v;
    printf("%-28s rms %6.1f  dev", pr["n"].str.c_str(), 10 * std::log10(ss / N));
    for (auto& g : G) { double s = 0; int c = 0; for (auto& [f, d] : diff) if (f >= g[0] && f <= g[1]) { s += d; c++; } printf("%6.1f", s / c - m0); }
    printf("   (%.3fx realtime)\n", (secs - .25) / 12);
    if (argc > 5) writeWav(argv[5], L, R, sr);
  }
  return 0;
}
