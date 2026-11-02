#include "PluginEditor.h"

using WBC = juce::WebBrowserComponent;

static juce::var arr(const float* v, int n) { juce::Array<juce::var> a; for (int i = 0; i < n; i++) a.add(v[i]); return a; }

static juce::String mime(const juce::String& ext) {
  static const std::map<juce::String, juce::String> m = {{"html", "text/html"}, {"js", "text/javascript"}, {"css", "text/css"}, {"json", "application/json"},
      {"woff2", "font/woff2"}, {"woff", "font/woff"}, {"ttf", "font/ttf"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"svg", "image/svg+xml"}, {"wav", "audio/wav"}};
  auto it = m.find(ext.toLowerCase()); return it == m.end() ? "application/octet-stream" : it->second;
}

std::optional<WBC::Resource> ApparitionEditor::serve(const juce::String& path) {
  auto pack = PackManager::get().current();
  if (!pack) return std::nullopt;
  auto rel = path.upToFirstOccurrenceOf("?", false, false).trimCharactersAtStart("/");
  if (rel.isEmpty()) rel = "index.html";
  auto root = pack->dir.getChildFile("ui"), f = root.getChildFile(rel);
  if (!f.isAChildOf(root) || !f.existsAsFile()) return std::nullopt;
  juce::MemoryBlock mb; f.loadFileAsData(mb);
  std::vector<std::byte> d(mb.getSize()); memcpy(d.data(), mb.getData(), mb.getSize());
  return WBC::Resource{std::move(d), mime(f.getFileExtension().substring(1))};
}

WBC::Options ApparitionEditor::options() {
  return WBC::Options{}
      .withBackend(WBC::Options::Backend::defaultBackend)
      .withNativeIntegrationEnabled()
      .withKeepPageLoadedWhenBrowserIsHidden()
      .withResourceProvider([this](const juce::String& p) { return serve(p); })
      .withNativeFunction("getState", [this](const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) { done(proc.getUIState()); })
      .withNativeFunction("setParam", [this](const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion done) { if (a.size() >= 2) proc.setFromUI(a[0].toString(), a[1]); done({}); })
      .withNativeFunction("setMeta", [this](const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion done) { if (a.size()) proc.setMeta(a[0]); done({}); })
      .withNativeFunction("spectrum", [this](const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion done) {
        auto* f = a.size() ? a[0].getArray() : nullptr; if (!f || f->isEmpty()) return done({});
        std::vector<float> fr, out((size_t)f->size()); for (auto& v : *f) fr.push_back((float)(double)v);
        if (!proc.spectrum(fr.data(), out.data(), (int)fr.size())) return done({});
        done(arr(out.data(), (int)out.size())); })
      .withNativeFunction("checkUpdate", [this](const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) {
        PackManager::get().checkForUpdate([this, done](PackManager::UpdateInfo u) {
          lastCheck = u;
          auto* o = new juce::DynamicObject();
          o->setProperty("ok", u.ok); o->setProperty("available", u.available); o->setProperty("needsShell", u.needsShell);
          o->setProperty("latest", u.latest); o->setProperty("notes", u.notes); o->setProperty("error", u.error); o->setProperty("shellUrl", u.shellUrl);
          auto cur = PackManager::get().current(); o->setProperty("current", cur ? cur->version : juce::String());
          done(juce::var(o)); }); })
      .withNativeFunction("installUpdate", [this](const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) {
        if (!lastCheck.available || lastCheck.needsShell) return done(juce::var("Kein installierbares Update"));
        juce::Component::SafePointer<ApparitionEditor> self(this);
        PackManager::get().installUpdate(lastCheck,
            [self](float f) { if (self) self->web.emitEventIfBrowserIsVisible("updateProgress", f); },
            [done](bool ok, juce::String err) { done(ok ? juce::var(true) : juce::var(err)); }); })
      .withNativeFunction("openURL", [](const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion done) {
        juce::URL u(a[0].toString()); if (u.getScheme() == "https") u.launchInDefaultBrowser(); done({}); })
      .withNativeFunction("revealPacks", [](const juce::Array<juce::var>&, WBC::NativeFunctionCompletion done) {
        auto d = PackManager::get().userPacksDir(); d.createDirectory(); d.revealToUser(); done({}); });
}

ApparitionEditor::ApparitionEditor(ApparitionProcessor& p) : AudioProcessorEditor(p), proc(p), web(options()) {
  addAndMakeVisible(web);
  setResizable(true, true);
  setResizeLimits(900, 620, 2400, 1600);
  setSize(1280, 820);
  proc.onHostParam = [this](const juce::String& id, const juce::var& v) { auto* o = new juce::DynamicObject(); o->setProperty("id", id); o->setProperty("v", v); web.emitEventIfBrowserIsVisible("hostParam", juce::var(o)); };
  PackManager::get().addChangeListener(this);
  web.goToURL(WBC::getResourceProviderRoot());
  startTimerHz(30);
}

ApparitionEditor::~ApparitionEditor() {
  proc.onHostParam = nullptr;
  PackManager::get().removeChangeListener(this);
}

void ApparitionEditor::timerCallback() {
  float m[7];
  if (proc.meters(m, 7)) web.emitEventIfBrowserIsVisible("meters", arr(m, 7));
}
