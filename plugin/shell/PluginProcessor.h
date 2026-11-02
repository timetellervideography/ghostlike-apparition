#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "PackManager.h"

// One engine instance bound to the pack it came from (keeps the pack's dylib alive).
struct EngineSlot {
  std::shared_ptr<const Pack> pack;
  GLEngine* eng = nullptr;
  ~EngineSlot() { if (eng) pack->api->destroy(eng); }
};

class ApparitionProcessor : public juce::AudioProcessor, private juce::ChangeListener, private juce::Timer {
 public:
  ApparitionProcessor();
  ~ApparitionProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout&) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return "Ghostlike Apparition"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 12.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  // ---- UI bridge (message thread)
  void setFromUI(const juce::String& id, const juce::var& v);
  juce::var getUIState() const;
  void setMeta(const juce::var& m) { meta = m; }
  bool meters(float* out, int n);
  bool spectrum(const float* f, float* out, int n);
  std::function<void(const juce::String&, const juce::var&)> onHostParam;  // automation → UI

  juce::AudioProcessorValueTreeState apvts;

 private:
  void changeListenerCallback(juce::ChangeBroadcaster*) override { rebuildEngine(); }
  void timerCallback() override;
  void rebuildEngine();
  void push(GLEngine* e, const std::shared_ptr<const Pack>& p, const juce::String& id, const juce::var& v);

  juce::NamedValueSet params;  // every engine parameter (numbers, or strings for choices); message thread
  juce::var meta;              // UI-only state: preset index/name, dirty flag ...
  double sr = 48000; int block = 512;
  std::atomic<EngineSlot*> live{nullptr}, incoming{nullptr}, retired{nullptr};
  std::vector<float> lastAuto;
  juce::HeapBlock<float> monoTmp;
};
