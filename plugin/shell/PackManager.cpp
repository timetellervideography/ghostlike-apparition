#include "PackManager.h"

#include <dlfcn.h>
#include <sys/xattr.h>

#include <juce_cryptography/juce_cryptography.h>

PackManager* PackManager::instance = nullptr;
PackManager& PackManager::get() { if (!instance) instance = new PackManager(); return *instance; }

PackManager::PackManager() { rescan(); }
PackManager::~PackManager() { pool.removeAllJobs(true, 5000); instance = nullptr; }

juce::File PackManager::userPacksDir() const {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/Ghostlike/Apparition/packs");
}

juce::File PackManager::factoryDir() const {
  // .../<bundle>/Contents/MacOS/<binary>  →  .../<bundle>/Contents/Resources/factory
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getSiblingFile("Resources").getChildFile("factory");
}

int PackManager::compareVersions(const juce::String& a, const juce::String& b) {
  auto pa = juce::StringArray::fromTokens(a, ".", ""), pb = juce::StringArray::fromTokens(b, ".", "");
  for (int i = 0; i < juce::jmax(pa.size(), pb.size()); i++) {
    int x = pa[i].getIntValue(), y = pb[i].getIntValue();
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

std::shared_ptr<Pack> PackManager::load(const juce::File& dir, bool factory) {
  auto meta = juce::JSON::parse(dir.getChildFile("pack.json"));
  if (!meta.isObject()) return nullptr;
  if ((int)meta["abi"] != GL_ENGINE_ABI) return nullptr;
  if (compareVersions(meta["minShell"].toString(), shellVersion()) > 0) return nullptr;
  auto lib = dir.getChildFile("libGhostlikeEngine.dylib");
  if (!lib.existsAsFile() || !dir.getChildFile("ui/index.html").existsAsFile()) return nullptr;
  // the dylib was written by us; make sure Gatekeeper's quarantine flag can't block dlopen
  removexattr(lib.getFullPathName().toRawUTF8(), "com.apple.quarantine", 0);
  void* h = dlopen(lib.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
  if (!h) { error = juce::String("Engine konnte nicht geladen werden: ") + juce::String(juce::CharPointer_UTF8(dlerror())); return nullptr; }
  auto entry = (GLEngineEntry)dlsym(h, GL_ENGINE_ENTRY);
  const GLEngineAPI* api = entry ? entry() : nullptr;
  if (!api || api->abi != GL_ENGINE_ABI) return nullptr;
  auto p = std::make_shared<Pack>();
  p->dir = dir; p->version = meta["version"].toString(); p->api = api; p->factory = factory; p->updateUrl = meta["updateUrl"].toString();
  p->engineJson = dir.getChildFile("engine.json").loadFileAsString();
  // reject packs whose engine refuses the config
  if (GLEngine* e = api->create(48000, 512, p->engineJson.toRawUTF8())) api->destroy(e); else return nullptr;
  return p;
}

void PackManager::rescan() {
  struct Cand { juce::File dir; juce::String ver; bool factory; };
  std::vector<Cand> cands;
  auto addDir = [&](const juce::File& d, bool f) { auto m = juce::JSON::parse(d.getChildFile("pack.json")); if (m.isObject()) cands.push_back({d, m["version"].toString(), f}); };
  addDir(factoryDir(), true);
  for (auto& d : userPacksDir().findChildFiles(juce::File::findDirectories, false)) addDir(d, false);
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { int c = compareVersions(a.ver, b.ver); return c != 0 ? c > 0 : !a.factory; });
  for (auto& c : cands) {
    { const juce::ScopedLock l(lock); if (pack && pack->dir == c.dir) return; }
    if (auto p = load(c.dir, c.factory)) {
      { const juce::ScopedLock l(lock); keepAlive.push_back(p); pack = p; }
      sendChangeMessage();
      return;
    }
  }
}

// override file (for testing) > current pack's updateUrl > compiled default
juce::String PackManager::updateUrl() const {
  auto ov = userPacksDir().getSiblingFile("update-url.txt");
  if (ov.existsAsFile()) return ov.loadFileAsString().trim();
  if (auto p = current(); p && p->updateUrl.startsWith("https://")) return p->updateUrl;
  return GL_UPDATE_URL;
}

void PackManager::checkForUpdate(std::function<void(UpdateInfo)> done) {
  pool.addJob([this, done, url = updateUrl()] {
    UpdateInfo u;
    int status = 0;
    auto stream = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(8000).withNumRedirectsToFollow(5).withStatusCode(&status));
    if (!stream || status >= 400) u.error = status ? "Update-Server antwortet nicht (HTTP " + juce::String(status) + ")" : "Keine Verbindung zum Update-Server";
    else {
      auto m = juce::JSON::parse(stream->readEntireStreamAsString());
      if (!m.isObject()) u.error = "Update-Info ist ungültig";
      else {
        u.ok = true; u.latest = m["version"].toString(); u.notes = m["notes"].toString();
        u.packUrl = m["pack"]["url"].toString(); u.packSha = m["pack"]["sha256"].toString(); u.size = (juce::int64)m["pack"]["size"];
        u.shellUrl = m["shell"]["url"].toString();
        u.needsShell = (int)m["abi"] != GL_ENGINE_ABI || compareVersions(m["minShell"].toString(), shellVersion()) > 0;
        auto cur = current();
        u.available = !cur || compareVersions(u.latest, cur->version) > 0;
      }
    }
    juce::MessageManager::callAsync([done, u] { done(u); });
  });
}

void PackManager::installUpdate(const UpdateInfo& u, std::function<void(float)> progress, std::function<void(bool, juce::String)> done) {
  pool.addJob([this, u, progress, done] {
    auto fail = [done](juce::String e) { juce::MessageManager::callAsync([done, e] { done(false, e); }); };
    auto tmp = juce::File::createTempFile(".zip");
    {
      int status = 0;
      auto in = juce::URL(u.packUrl).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(15000).withNumRedirectsToFollow(5).withStatusCode(&status));
      if (!in || status >= 400) return fail("Download fehlgeschlagen");
      juce::FileOutputStream out(tmp); if (!out.openedOk()) return fail("Kann temporäre Datei nicht schreiben");
      juce::HeapBlock<char> buf(1 << 16); juce::int64 got = 0, total = u.size > 0 ? u.size : in->getTotalLength();
      for (;;) { auto n = in->read(buf, 1 << 16); if (n <= 0) break; out.write(buf, (size_t)n); got += n;
        if (total > 0) { float f = (float)got / (float)total; juce::MessageManager::callAsync([progress, f] { progress(juce::jmin(1.f, f)); }); } }
    }
    if (u.packSha.isNotEmpty() && juce::SHA256(tmp).toHexString() != u.packSha.toLowerCase()) { tmp.deleteFile(); return fail("Prüfsumme stimmt nicht – Download beschädigt"); }
    auto dest = userPacksDir().getChildFile(u.latest), stage = userPacksDir().getChildFile("." + u.latest + ".tmp");
    stage.deleteRecursively(); stage.createDirectory();
    juce::ZipFile zip(tmp);
    if (zip.getNumEntries() == 0 || zip.uncompressTo(stage).failed()) { tmp.deleteFile(); stage.deleteRecursively(); return fail("Paket konnte nicht entpackt werden"); }
    tmp.deleteFile();
    dest.deleteRecursively();
    if (!stage.moveFileTo(dest)) return fail("Paket konnte nicht installiert werden");
    juce::MessageManager::callAsync([this, done, dest] {
      rescan();
      auto cur = current();
      if (cur && cur->dir == dest) done(true, {});
      else done(false, "Neue Version ist installiert, konnte aber nicht geladen werden: " + error);
    });
  });
}
