/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

    Rewritten to host the look-ahead compressor engine, its parameters and the
    realtime meters used by the GUI.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "Compressor.h"

//==============================================================================
class CompressorAudioProcessor : public juce::AudioProcessor {
 public:
  //==============================================================================
  CompressorAudioProcessor();
  ~CompressorAudioProcessor() override;

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  //==============================================================================
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  //==============================================================================
  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  //==============================================================================
  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  //==============================================================================
  juce::AudioProcessorValueTreeState apvts;

  // Realtime meter readouts (audio thread writes, editor polls at ~30 Hz)
  float getGainReductionDb() const noexcept { return gainReductionDb_.load(); }
  float getInputLevelDb() const noexcept { return inputLevelDb_.load(); }
  float getOutputLevelDb() const noexcept { return outputLevelDb_.load(); }

  // Wires/unwires the reserved analog-modeling tap point (editor UI only).
  void setSaturationEnabled(bool enabled);

 private:
  //==============================================================================
  juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  void updateCompressorParameters();
  void updateMeters();

  //==============================================================================
  Compressor compressor_;

  // Cached parameter pointers (read on the audio thread)
  juce::AudioParameterFloat *paramInGain_ = nullptr;
  juce::AudioParameterFloat *paramOutGain_ = nullptr;
  juce::AudioParameterFloat *paramThreshold_ = nullptr;
  juce::AudioParameterChoice *paramRatio_ = nullptr;
  juce::AudioParameterFloat *paramKnee_ = nullptr;
  juce::AudioParameterFloat *paramHpf_ = nullptr;
  juce::AudioParameterChoice *paramAttack_ = nullptr;
  juce::AudioParameterChoice *paramRelease_ = nullptr;
  juce::AudioParameterFloat *paramLookahead_ = nullptr;
  juce::AudioParameterBool *paramLookaheadOn_ = nullptr;
  juce::AudioParameterBool *paramFeedbackMode_ = nullptr;
  juce::AudioParameterFloat *paramMix_ = nullptr;
  juce::AudioParameterBool *paramBypass_ = nullptr;

  std::atomic<float> gainReductionDb_{-0.001f};
  std::atomic<float> inputLevelDb_{-120.0f};
  std::atomic<float> outputLevelDb_{-120.0f};

  std::atomic<double> sampleRate_{44100.0};
  int reportedLatency_ = -1;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorAudioProcessor)
};