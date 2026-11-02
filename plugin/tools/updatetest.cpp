// Native end-to-end test of the update path (same PackManager code as the plugin):
// check manifest → download → sha256 → unzip → load engine → process audio.
#include "PackManager.h"
#include <juce_events/juce_events.h>

int main() {
  juce::ScopedJuceInitialiser_GUI init;
  auto& pm = PackManager::get();
  auto cur = pm.current();
  printf("before: %s\n", cur ? cur->version.toRawUTF8() : "(none)");
  bool finished = false; int rc = 1;
  pm.checkForUpdate([&](PackManager::UpdateInfo u) {
    printf("check: ok=%d available=%d latest=%s needsShell=%d err=%s\n", u.ok, u.available, u.latest.toRawUTF8(), u.needsShell, u.error.toRawUTF8());
    if (!u.ok || !u.available) { finished = true; return; }
    pm.installUpdate(u, [](float f) { if (f >= 1) printf("download 100%%\n"); }, [&](bool ok, juce::String err) {
      auto p = pm.current();
      printf("install: ok=%d err=%s now=%s dir=%s\n", ok, err.toRawUTF8(), p ? p->version.toRawUTF8() : "-", p ? p->dir.getFullPathName().toRawUTF8() : "-");
      if (ok && p) {
        GLEngine* e = p->api->create(48000, 256, p->engineJson.toRawUTF8());
        p->api->setChoice(e, "amp", "ravager");
        std::vector<float> L(256), R(256); double acc = 0;
        for (int b = 0; b < 400; b++) { for (int i = 0; i < 256; i++) L[i] = R[i] = 0.2f * std::sin(0.05f * (b * 256 + i)); float* io[2] = {L.data(), R.data()}; p->api->process(e, io, 2, 256); for (float v : L) acc += v * v; }
        printf("engine %s processes audio: rms %.1f dB\n", p->api->version, 10 * std::log10(acc / (400 * 256)));
        p->api->destroy(e); rc = 0;
      }
      finished = true; });
  });
  while (!finished) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
  return rc;
}
