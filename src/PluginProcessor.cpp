/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

    Rewritten to host the look-ahead compressor engine, its parameters and the
    realtime meters used by the GUI.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {

juce::String formatDb(float value, int precision = 1) {
  return juce::String(value, precision) + " dB";
}

juce::String formatTime(float value, int precision = 1) {
  return juce::String(value, precision) + " ms";
}

juce::String formatPercent(float value) {
  return juce::String(juce::roundToInt(value * 100.0f)) + " %";
}

// Choice -> real value mappings (must match the label arrays in
// createParameterLayout()).
// Ratio: 2, 3, 4, 5, 6, 8, 10, 20, infinity (a large value acts as a limiter).
constexpr float ratioValues[] = {2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
                                 8.0f, 10.0f, 20.0f, 1000.0f};
// Attack: 1, 3, 5, 10, 15, 20, 30, 50, 100, 200 ms.
constexpr float attackValues[] = {1.0f, 3.0f, 5.0f, 10.0f, 15.0f, 20.0f,
                                  30.0f, 50.0f, 100.0f, 200.0f};
// Release: 20, 50, 100, 200, 300, 400, 500, 600, 800, 1000, 2000 ms, AUTO.
// AUTO is a sentinel (-1) that enables the program-dependent release.
constexpr float releaseValues[] = {20.0f, 50.0f, 100.0f, 200.0f, 300.0f,
                                   400.0f, 500.0f, 600.0f, 800.0f, 1000.0f,
                                   2000.0f, -1.0f};

int clampIndex(int index, int size) { return juce::jlimit(0, size - 1, index); }

// Look-ahead delay ceiling shared by the UI range, the DSP capacity and the
// constant host-latency figure reported while the lookahead switch is on.
constexpr float kMaxLookaheadMs = 10.0f;

}  // namespace

//==============================================================================
CompressorAudioProcessor::CompressorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : juce::AudioProcessor(
          juce::AudioProcessor::BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
              ),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
#else
    : apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
  paramInGain_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("input"));
  paramOutGain_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("output"));
  paramThreshold_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("threshold"));
  paramRatio_ = dynamic_cast<juce::AudioParameterChoice *>(apvts.getParameter("ratio"));
  paramKnee_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("knee"));
  paramHpf_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("hpf"));
  paramAttack_ = dynamic_cast<juce::AudioParameterChoice *>(apvts.getParameter("attack"));
  paramRelease_ = dynamic_cast<juce::AudioParameterChoice *>(apvts.getParameter("release"));
  paramLookahead_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("lookahead"));
  paramLookaheadOn_ = dynamic_cast<juce::AudioParameterBool *>(apvts.getParameter("lookaheadOn"));
  paramFeedbackMode_ = dynamic_cast<juce::AudioParameterBool *>(apvts.getParameter("feedback"));
  paramMix_ = dynamic_cast<juce::AudioParameterFloat *>(apvts.getParameter("mix"));
  paramBypass_ = dynamic_cast<juce::AudioParameterBool *>(apvts.getParameter("bypass"));
}

CompressorAudioProcessor::~CompressorAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
CompressorAudioProcessor::createParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  using APF = juce::AudioParameterFloat;
  using APC = juce::AudioParameterChoice;
  using APB = juce::AudioParameterBool;
  using Range = juce::NormalisableRange<float>;
  using Attr = juce::AudioParameterFloatAttributes;

  const auto id = [](const char *n) { return juce::ParameterID(n, 1); };

  layout.add(std::make_unique<APF>(
      id("input"), "Input Gain", Range(-24.0f, 24.0f, 0.01f), 0.0f,
      Attr().withLabel("dB").withStringFromValueFunction(
          [](float v, int) { return formatDb(v); })));

  layout.add(std::make_unique<APF>(
      id("output"), "Output Gain", Range(-24.0f, 24.0f, 0.01f), 0.0f,
      Attr().withLabel("dB").withStringFromValueFunction(
          [](float v, int) { return formatDb(v); })));

  layout.add(std::make_unique<APF>(
      id("threshold"), "Threshold", Range(-60.0f, 4.0f, 0.5f), 4.0f,
      Attr().withLabel("dB").withStringFromValueFunction(
          [](float v, int) { return formatDb(v, 1); })));

  layout.add(std::make_unique<APC>(
      id("ratio"), "Ratio",
      juce::StringArray{"2 : 1", "3 : 1", "4 : 1", "5 : 1", "6 : 1",
                        "8 : 1", "10 : 1", "20 : 1",
                        juce::String::fromUTF8("\xE2" "\x88" "\x9E") + " : 1"},
      2));  // default 4 : 1

  layout.add(std::make_unique<APF>(
      id("knee"), "Knee", Range(0.0f, 24.0f, 0.1f, 0.5f), 12.0f,
      Attr().withLabel("dB").withStringFromValueFunction(
          [](float v, int) { return formatDb(v, 0); })));

  layout.add(std::make_unique<APF>(
      id("hpf"), "Sidechain HPF", Range(0.0f, 500.0f, 1.0f, 0.35f), 0.0f,
      Attr().withLabel("Hz").withStringFromValueFunction(
          [](float v, int) {
            return v <= 0.0f
                       ? "Off"
                       : juce::String(juce::roundToInt(v)) + " Hz";
          })));

  layout.add(std::make_unique<APC>(
      id("attack"), "Attack",
      juce::StringArray{"1 ms", "3 ms", "5 ms", "10 ms", "15 ms", "20 ms",
                        "30 ms", "50 ms", "100 ms", "200 ms"},
      3));  // default 10 ms

  layout.add(std::make_unique<APC>(
      id("release"), "Release",
      juce::StringArray{"20 ms", "50 ms", "100 ms", "200 ms", "300 ms",
                        "400 ms", "500 ms", "600 ms", "800 ms", "1 s",
                        "2 s", "AUTO"},
      2));  // default 100 ms

  layout.add(std::make_unique<APF>(
      id("lookahead"), "Lookahead", Range(0.0f, kMaxLookaheadMs, 0.1f, 0.5f),
      0.0f,
      Attr().withLabel("ms").withStringFromValueFunction(
          [](float v, int) { return formatTime(v, 1); })));

  // Master switch for the look-ahead feature: when off the lookahead knob is
  // ignored (0 ms delay); when on the knob works normally (0..10 ms) and the
  // plugin reports a constant 10 ms latency to the host.
  layout.add(std::make_unique<APB>(id("lookaheadOn"), "Lookahead On", false));

  // Feed-forward (off) vs feedback (on) detection topology.
  layout.add(std::make_unique<APB>(id("feedback"), "Feedback Mode", false));

  layout.add(std::make_unique<APF>(
      id("mix"), "Mix", Range(0.0f, 1.0f, 0.01f), 1.0f,
      Attr().withStringFromValueFunction(
          [](float v, int) { return formatPercent(v); })));

  layout.add(std::make_unique<APB>(id("bypass"), "Bypass", false));

  return layout;
}

//==============================================================================
const juce::String CompressorAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool CompressorAudioProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
  return true;
#else
  return false;
#endif
}

bool CompressorAudioProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
  return true;
#else
  return false;
#endif
}

bool CompressorAudioProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
  return true;
#else
  return false;
#endif
}

double CompressorAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int CompressorAudioProcessor::getNumPrograms() {
  return 1;
}

int CompressorAudioProcessor::getCurrentProgram() { return 0; }

void CompressorAudioProcessor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}

const juce::String CompressorAudioProcessor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}

void CompressorAudioProcessor::changeProgramName(int index,
                                                 const juce::String &newName) {
  juce::ignoreUnused(index, newName);
}

//==============================================================================
void CompressorAudioProcessor::prepareToPlay(double sampleRate,
                                             int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);
  sampleRate_.store(sampleRate);
  compressor_.prepare(sampleRate, std::max(1, getTotalNumInputChannels()));

  const auto resetMeters = [this]() {
    gainReductionDb_.store(-0.001f);
    inputLevelDb_.store(-120.0f);
    outputLevelDb_.store(-120.0f);
  };
  resetMeters();

  updateCompressorParameters();
}

void CompressorAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CompressorAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
#if JucePlugin_IsMidiEffect
  juce::ignoreUnused(layouts);
  return true;
#else
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

#if !JucePlugin_IsSynth
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}
#endif

//==============================================================================
void CompressorAudioProcessor::updateCompressorParameters() {
  if (paramInGain_ != nullptr)
    compressor_.setInputGainDb(paramInGain_->get());
  if (paramOutGain_ != nullptr)
    compressor_.setOutputGainDb(paramOutGain_->get());
  if (paramThreshold_ != nullptr)
    compressor_.setThresholdDb(paramThreshold_->get());
  if (paramRatio_ != nullptr)
    compressor_.setRatio(ratioValues[clampIndex(
        paramRatio_->getIndex(), (int)std::size(ratioValues))]);
  if (paramKnee_ != nullptr)
    compressor_.setKneeDb(paramKnee_->get());
  if (paramHpf_ != nullptr)
    compressor_.setSidechainHpfHz(paramHpf_->get());
  if (paramAttack_ != nullptr)
    compressor_.setAttackMs(attackValues[clampIndex(
        paramAttack_->getIndex(), (int)std::size(attackValues))]);
  if (paramRelease_ != nullptr) {
    const float rel = releaseValues[clampIndex(
        paramRelease_->getIndex(), (int)std::size(releaseValues))];
    if (rel < 0.0f) {
      compressor_.setReleaseAutoEnabled(true);
    } else {
      compressor_.setReleaseAutoEnabled(false);
      compressor_.setReleaseMs(rel);
    }
  }
  const bool laOn =
      paramLookaheadOn_ != nullptr && paramLookaheadOn_->get();
  if (paramLookahead_ != nullptr)
    compressor_.setLookaheadMs(laOn ? paramLookahead_->get() : 0.0f);
  if (paramFeedbackMode_ != nullptr)
    compressor_.setFeedbackMode(paramFeedbackMode_->get());
  if (paramMix_ != nullptr)
    compressor_.setMix(paramMix_->get());
  if (paramBypass_ != nullptr)
    compressor_.setBypass(paramBypass_->get());

  // The look-ahead delay line adds delay to the audio path; keep the host's
  // latency figure in sync so it applies the correct delay compensation to the
  // other tracks. Report the *actual* delay: reporting a constant maximum here
  // made the track play early (the host compensated for 10 ms while the audio
  // was only delayed by the chosen knob value).
  const float laMs =
      (paramLookahead_ != nullptr && laOn) ? paramLookahead_->get() : 0.0f;
  const int latency =
      juce::roundToInt(laMs * 0.001 * sampleRate_.load());
  if (latency != reportedLatency_) {
    reportedLatency_ = latency;
    setLatencySamples(latency);
  }
}

//==============================================================================
void CompressorAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                            juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(midiMessages);

  const auto totalNumInputChannels = getTotalNumInputChannels();
  const auto totalNumOutputChannels = getTotalNumOutputChannels();
  const auto numSamples = buffer.getNumSamples();

  if (totalNumOutputChannels == 0)
    return;

  for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, numSamples);

  updateCompressorParameters();

  if (totalNumInputChannels > 0 && numSamples > 0)
    compressor_.process(buffer, totalNumInputChannels, numSamples);

  updateMeters();
}

void CompressorAudioProcessor::updateMeters() {
  gainReductionDb_.store(compressor_.getGainReductionDb());
  inputLevelDb_.store(compressor_.getInputLevelDb());
  outputLevelDb_.store(compressor_.getOutputLevelDb());
}

//==============================================================================
bool CompressorAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *CompressorAudioProcessor::createEditor() {
  return new CompressorAudioProcessorEditor(*this);
}

//==============================================================================
void CompressorAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  juce::MemoryOutputStream stream(destData, false);
  apvts.state.writeToStream(stream);
}

void CompressorAudioProcessor::setStateInformation(const void *data,
                                                   int sizeInBytes) {
  auto tree = juce::ValueTree::readFromData(data, (size_t)sizeInBytes);
  if (!tree.isValid())
    return;
  apvts.replaceState(tree);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new CompressorAudioProcessor();
}