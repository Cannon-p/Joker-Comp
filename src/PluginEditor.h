/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

    Rewritten to present the compressor controls with custom-painted widgets
    and realtime VU meters.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "GuiComponents.h"
#include "PluginProcessor.h"

//==============================================================================
/**
 */
class CompressorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::Timer {
 public:
  CompressorAudioProcessorEditor(CompressorAudioProcessor &);
  ~CompressorAudioProcessorEditor() override;

  //==============================================================================
  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;

 private:
  //==============================================================================
  SleekRotary *addKnob(const char *paramId, const juce::String &caption,
                       std::function<juce::String(float)> formatter);
  void addModelingButton(const juce::String &caption, bool wiredToDsp);
  void updateLookaheadKnobState();

  juce::OwnedArray<SleekRotary> knobs_;
  juce::OwnedArray<juce::Label> knobLabels_;
  juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment>
      sliderAttachments_;

  PowerSwitch bypassSwitch_{"BYPASS"};
  juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment_;

  // Lookahead master switch (below the LOOKAHEAD knob): off freezes the knob.
  PowerSwitch lookaheadSwitch_{"LOOKAHEAD"};
  juce::AudioProcessorValueTreeState::ButtonAttachment lookaheadAttachment_;
  SleekRotary *lookaheadKnob_ = nullptr;

  // Feed-forward / feedback detection topology switch (right of the panel
  // header): off = feed-forward, on = feedback.
  PowerSwitch feedbackSwitch_{"FEEDBACK"};
  juce::AudioProcessorValueTreeState::ButtonAttachment feedbackAttachment_;

  // Panels
  Panel mainPanel_{"Compressor"};
  Panel meterPanel_{"GR"};
  Panel modelPanel_{"Analog Modeling"};

  // Reserved analog-modeling stage toggles (future development).
  juce::OwnedArray<PowerSwitch> modelButtons_;

  // Meters
  VuMeter grMeter_{VuMeter::Mode::gainReduction, ""};

  float grPeakHold_ = 0.0f;

  // This reference is provided as a quick way for your editor to
  // access the processor object that created it.
  CompressorAudioProcessor &audioProcessor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      CompressorAudioProcessorEditor)
};