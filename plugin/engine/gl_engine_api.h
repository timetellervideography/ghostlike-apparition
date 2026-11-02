// Ghostlike Apparition – C ABI between the installed plugin shell and the hot-swappable DSP engine (libGhostlikeEngine.dylib).
// The shell never links the engine: it dlopen()s the newest compatible build from the content folder, so sound updates
// arrive through the in-plugin update button. Bump GL_ENGINE_ABI only when this struct changes (that needs a new shell).
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define GL_ENGINE_ABI 1
#define GL_ENGINE_ENTRY "gl_engine_api"

typedef struct GLEngine GLEngine;

typedef struct GLEngineAPI {
  int abi;
  const char* version;
  // engine.json from the content pack: parameters, amp models, tonestacks, cabs. Returns NULL on a bad config.
  GLEngine* (*create)(double sampleRate, int maxBlock, const char* configJson);
  void (*destroy)(GLEngine*);
  // any thread; values are picked up at the start of the next process() call
  void (*setParam)(GLEngine*, const char* id, double value);
  int  (*setChoice)(GLEngine*, const char* id, const char* value);   // enum params by name ("amp" = "ravager")
  double (*getParam)(GLEngine*, const char* id);
  void (*process)(GLEngine*, float* const* channels, int numChannels, int numSamples);
  void (*reset)(GLEngine*);
  int  (*latency)(GLEngine*);
  // [inPeak, inRms, outPeak, outRms, prePeak (before limiter), gateState 0/1/2, glow 0..1]
  int  (*meters)(GLEngine*, float* out, int max);
  // magnitude spectrum of the output in dB at the given frequencies (caller thread, not realtime)
  int  (*spectrum)(GLEngine*, const float* freqs, float* outDb, int count);
} GLEngineAPI;

typedef const GLEngineAPI* (*GLEngineEntry)(void);

#ifdef __cplusplus
}
#endif
