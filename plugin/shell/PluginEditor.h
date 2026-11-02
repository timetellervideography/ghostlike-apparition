#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

// The whole interface is the content pack's ui/index.html in a WebView, so its look updates with the pack.
class ApparitionEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener {
 public:
  explicit ApparitionEditor(ApparitionProcessor&);
  ~ApparitionEditor() override;
  void resized() override { web.setBounds(getLocalBounds()); }

 private:
  void timerCallback() override;
  void changeListenerCallback(juce::ChangeBroadcaster*) override { web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot()); }
  std::optional<juce::WebBrowserComponent::Resource> serve(const juce::String& path);
  juce::WebBrowserComponent::Options options();

  ApparitionProcessor& proc;
  PackManager::UpdateInfo lastCheck;
  juce::WebBrowserComponent web;
};
