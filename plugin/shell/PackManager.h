// Content packs: everything that can change through the update button (DSP engine dylib, amp data, presets, UI).
// The factory pack ships inside the plugin bundle; downloaded packs live in
// ~/Library/Application Support/Ghostlike/Apparition/packs/<version>. The newest compatible pack wins.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "gl_engine_api.h"

struct Pack {
  juce::File dir;
  juce::String version;
  juce::String updateUrl;  // a pack may move the update server
  const GLEngineAPI* api = nullptr;
  juce::String engineJson;
  bool factory = false;
};

class PackManager : public juce::ChangeBroadcaster, private juce::DeletedAtShutdown {  // torn down with JUCE, not by static destructors
 public:
  static PackManager& get();

  std::shared_ptr<const Pack> current() const { const juce::ScopedLock l(lock); return pack; }
  juce::File userPacksDir() const;
  juce::File factoryDir() const;
  void rescan();  // picks the newest valid pack and broadcasts a change if it differs

  // ---- updates (results are delivered on the message thread)
  struct UpdateInfo { bool ok = false, available = false, needsShell = false; juce::String latest, notes, error, packUrl, packSha, shellUrl; juce::int64 size = 0; };
  void checkForUpdate(std::function<void(UpdateInfo)> done);
  void installUpdate(const UpdateInfo& u, std::function<void(float)> progress, std::function<void(bool, juce::String)> done);
  juce::String lastError() const { return error; }
  juce::String updateUrl() const;

  static int compareVersions(const juce::String& a, const juce::String& b);
  static const char* shellVersion() { return GL_SHELL_VERSION; }

 private:
  PackManager();
  ~PackManager() override;
  static PackManager* instance;
  std::shared_ptr<Pack> load(const juce::File& dir, bool factory);
  mutable juce::CriticalSection lock;
  std::shared_ptr<const Pack> pack;
  std::vector<std::shared_ptr<Pack>> keepAlive;  // engines stay mapped: old instances may still run on them
  juce::String error;
  juce::ThreadPool pool{1};
};
