#include "PluginProcessor.h"
#include "PluginEditor.h"

// Parameters the DAW can automate. Everything else (amp model, cab, mic, FX switches …) is plugin state.
// Ranges mirror engine.json; they are fixed in the shell because hosts expect a stable parameter list.
struct AutoParam { const char* id; const char* name; float min, max, def; };
static const AutoParam AUTO[] = {
    {"inGain", "Input", -12, 12, 0}, {"outGain", "Output", -24, 12, -3}, {"gateThr", "Gate", -90, -10, -55},
    {"gain", "Gain", 0, 11, 7}, {"bass", "Bass", 0, 11, 5.5f}, {"mid", "Middle", 0, 11, 5.5f}, {"treble", "Treble", 0, 11, 6.5f},
    {"presence", "Presence", 0, 11, 6.5f}, {"depth", "Depth", 0, 11, 6}, {"master", "Master", 0, 11, 6.6f},
    {"dlyMix", "Delay Mix", 0, 60, 20}, {"revMix", "Reverb Mix", 0, 70, 8}};
static constexpr int NAUTO = (int)(sizeof(AUTO) / sizeof(AUTO[0]));

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  for (auto& a : AUTO) l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{a.id, 1}, a.name, juce::NormalisableRange<float>(a.min, a.max, 0.01f), a.def));
  return l;
}
static int autoIndex(const juce::String& id) { for (int i = 0; i < NAUTO; i++) if (id == AUTO[i].id) return i; return -1; }

ApparitionProcessor::ApparitionProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", layout()) {
  lastAuto.assign(NAUTO, std::numeric_limits<float>::quiet_NaN());
  for (auto& a : AUTO) params.set(a.id, (double)a.def);
  PackManager::get().addChangeListener(this);
  startTimerHz(10);
}

ApparitionProcessor::~ApparitionProcessor() {
  PackManager::get().removeChangeListener(this);
  delete live.exchange(nullptr); delete incoming.exchange(nullptr); delete retired.exchange(nullptr);
}

bool ApparitionProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
  auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
  if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
  return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ApparitionProcessor::push(GLEngine* e, const std::shared_ptr<const Pack>& p, const juce::String& id, const juce::var& v) {
  if (!e) return;
  if (v.isString()) p->api->setChoice(e, id.toRawUTF8(), v.toString().toRawUTF8());
  else p->api->setParam(e, id.toRawUTF8(), (double)v);
}

void ApparitionProcessor::rebuildEngine() {
  auto pack = PackManager::get().current();
  if (!pack) return;
  auto* slot = new EngineSlot();
  slot->pack = pack;
  slot->eng = pack->api->create(sr, block, pack->engineJson.toRawUTF8());
  if (!slot->eng) { delete slot; return; }
  for (auto& nv : params) push(slot->eng, pack, nv.name.toString(), nv.value);
  setLatencySamples(pack->api->latency(slot->eng));
  for (auto& v : lastAuto) v = std::numeric_limits<float>::quiet_NaN();
  if (!live.load()) live.store(slot);
  else delete incoming.exchange(slot);  // audio thread picks it up at the next block
}

void ApparitionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  sr = sampleRate; block = samplesPerBlock;
  monoTmp.allocate((size_t)juce::jmax(1, samplesPerBlock), true);
  delete incoming.exchange(nullptr);
  delete live.exchange(nullptr);
  rebuildEngine();
}

void ApparitionProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer&) {
  juce::ScopedNoDenormals nd;
  // hot swap after an update: only when the previous retired engine has been collected
  if (incoming.load() && !retired.load()) if (auto* in = incoming.exchange(nullptr)) retired.store(live.exchange(in));
  auto* slot = live.load();
  const int n = buf.getNumSamples(), nin = getTotalNumInputChannels(), nout = getTotalNumOutputChannels();
  if (!slot || !slot->eng) { buf.clear(); return; }
  for (int i = 0; i < NAUTO; i++) {
    float v = apvts.getRawParameterValue(AUTO[i].id)->load();
    if (v != lastAuto[(size_t)i]) { lastAuto[(size_t)i] = v; slot->pack->api->setParam(slot->eng, AUTO[i].id, v); }
  }
  if (nout > 1 && nin == 1) buf.copyFrom(1, 0, buf, 0, 0, n);
  float* ch[2] = {buf.getWritePointer(0), nout > 1 ? buf.getWritePointer(1) : nullptr};
  slot->pack->api->process(slot->eng, ch, nout > 1 ? 2 : 1, n);
}

void ApparitionProcessor::timerCallback() {
  delete retired.exchange(nullptr);
  // host automation → stored state → UI
  for (auto& a : AUTO) {
    double v = apvts.getRawParameterValue(a.id)->load();
    if (std::abs((double)params[a.id] - v) > 1e-4) { params.set(a.id, v); if (onHostParam) onHostParam(a.id, v); }
  }
}

void ApparitionProcessor::setFromUI(const juce::String& id, const juce::var& v) {
  params.set(id, v);
  int ai = autoIndex(id);
  if (ai >= 0) {  // goes through the host so automation can be recorded; the audio thread forwards it
    auto* p = apvts.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1((float)(double)v));
    return;
  }
  if (auto* s = live.load()) push(s->eng, s->pack, id, v);
  if (auto* s = incoming.load()) push(s->eng, s->pack, id, v);
}

juce::var ApparitionProcessor::getUIState() const {
  auto* o = new juce::DynamicObject();
  auto* p = new juce::DynamicObject();
  for (auto& nv : params) p->setProperty(nv.name, nv.value);
  o->setProperty("params", p);
  o->setProperty("meta", meta);
  o->setProperty("shell", PackManager::shellVersion());
  auto pack = PackManager::get().current();
  o->setProperty("pack", pack ? pack->version : juce::String());
  o->setProperty("factory", pack && pack->factory);
  return o;
}

bool ApparitionProcessor::meters(float* out, int n) { auto* s = live.load(); return s && s->eng && s->pack->api->meters(s->eng, out, n) > 0; }
bool ApparitionProcessor::spectrum(const float* f, float* out, int n) { auto* s = live.load(); return s && s->eng && s->pack->api->spectrum(s->eng, f, out, n) > 0; }

void ApparitionProcessor::getStateInformation(juce::MemoryBlock& mb) {
  auto* o = new juce::DynamicObject();
  auto* p = new juce::DynamicObject();
  for (auto& nv : params) p->setProperty(nv.name, nv.value);
  o->setProperty("v", 1); o->setProperty("params", p); o->setProperty("meta", meta);
  juce::MemoryOutputStream(mb, false).writeString(juce::JSON::toString(juce::var(o), true));
}

void ApparitionProcessor::setStateInformation(const void* data, int size) {
  auto j = juce::JSON::parse(juce::String::fromUTF8((const char*)data, size));
  if (!j.isObject()) return;
  if (auto* p = j["params"].getDynamicObject())
    for (auto& nv : p->getProperties()) {
      params.set(nv.name, nv.value);
      if (int ai = autoIndex(nv.name.toString()); ai >= 0) { auto* ap = apvts.getParameter(nv.name.toString()); ap->setValueNotifyingHost(ap->convertTo0to1((float)(double)nv.value)); }
      else { if (auto* s = live.load()) push(s->eng, s->pack, nv.name.toString(), nv.value); if (auto* s = incoming.load()) push(s->eng, s->pack, nv.name.toString(), nv.value); }
    }
  meta = j["meta"];
}

juce::AudioProcessorEditor* ApparitionProcessor::createEditor() { return new ApparitionEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ApparitionProcessor(); }
