/*
  ==============================================================================

    Compressor.h
    Created: Aug 2026
    Author:  Joker_p

    A feed-forward look-ahead dynamics processor built on the JUCE dsp toolkit.

    Signal flow (sample accurate):

        input
          |-- input gain ----------------------------------------------+
          |                                                            |
          |  envelope detect (peak ballistics)                         |
          |  static curve  (threshold / ratio)   [knee=0, hard knee]   |
          |  look-ahead  (audio + gain aligned  via delay line)         |
          |                                                            |
          +->  wet path : delayed( inputGain * gain ) ----------------+
                               |                                      |
                               +-- dry/wet mix (both signals delayed) |
                                                      |
                                                      +-- output gain --> output

    The class keeps the signal chain free of any built-in analog-modeling /
    saturation stage; that engine will be added behind the upcoming model
    selector once developed.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <vector>

//==============================================================================
/**
    Envelope detector with a logarithmic ("fast-then-slow") release shape.

    Attack is a plain one-pole rise. Release uses a level-dependent time
    constant: right after a transient the envelope falls quickly, then the
    decay progressively slows as it settles onto the signal - giving the
    classic log-shaped tail instead of a constant-rate linear-in-dB release.
*/
class EnvelopeFollower {
 public:
  void prepare(double sr, int numChannels);
  void reset();
  void setAttackTime(float ms) { attackMs_ = std::max(0.01f, ms); }
  void setReleaseTime(float ms) { releaseMs_ = std::max(0.1f, ms); }
  /** How much slower the release tail gets vs. the initial (fast) phase. */
  void setReleaseLogDepth(float depth) { releaseLogDepth_ = std::max(0.0f, depth); }

  float processSample(int channel, float absIn);

 private:
  struct State {
    float env = 0.0f;
    float releasePeak = 0.0f;  // envelope level where the current release began
  };

  double sr_ = 44100.0;
  float attackMs_ = 10.0f;
  float releaseMs_ = 120.0f;
  float releaseLogDepth_ = 8.0f;
  std::vector<State> state_;
};

//==============================================================================
class Compressor {
 public:
  Compressor() = default;

  //==============================================================================
  void prepare(double sampleRate, int numChannels);
  void reset();

  //==============================================================================
  // Parameter setter - call from the audio callback. Values are internally
  // smoothed to avoid zipper noise.
  void setInputGainDb(float dB);
  void setOutputGainDb(float dB);
  void setThresholdDb(float dB);
  void setRatio(float ratio);
  void setKneeDb(float db);
  void setSidechainHpfHz(float hz);
  void setAttackMs(float ms);
  void setReleaseMs(float ms);
  /** Auto (program-dependent) release: fast for transient material, slow for
      sustained material. Overrides setReleaseMs while enabled. */
  void setReleaseAutoEnabled(bool enabled);
  void setLookaheadMs(float ms);
  /** Feed-forward (false, default) detects the input; feedback (true) detects
      the compressed output - the classic feedback topology whose gain reacts to
      what the compressor has already done rather than to the incoming signal. */
  void setFeedbackMode(bool enabled);
  void setMix(float mix);  // 0.0 = dry, 1.0 = fully wet
  void setBypass(bool bypass);
  void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }

  //==============================================================================
  // Extension point reserved for a future analog-modeling stage. The GUI's model
  // selector is UI-only for now; wiring the selected model into the DSP comes
  // with the analog engine itself.

  //==============================================================================
  // In-place processing of the whole buffer using maxChannels channels.
  void process(juce::AudioBuffer<float> &buffer, int numChannels,
               int numSamples);

  //==============================================================================
  // Meter readouts (post ballistics), suitable for a 30 Hz GUI timer.
  float getGainReductionDb() const noexcept { return grLevel_; }
  float getInputLevelDb() const noexcept { return inputLevel_; }
  float getOutputLevelDb() const noexcept { return outputLevel_; }

 private:
  //==============================================================================
  void updateSmoothed();

  //==============================================================================
  double sampleRate_ = 44100.0;

  // Targets (raw parameter values)
  float targetInputGainDb_ = 0.0f;
  float targetOutputGainDb_ = 0.0f;
  float targetThresholdDb_ = -24.0f;
  float targetRatio_ = 3.0f;
  float targetKneeDb_ = 6.0f;
  float targetSidechainHpfHz_ = 0.0f;
  float targetAttackMs_ = 10.0f;
  float targetReleaseMs_ = 120.0f;
  bool releaseAuto_ = false;
  float targetLookaheadMs_ = 3.0f;
  bool feedbackMode_ = false;
  float targetMix_ = 1.0f;
  bool bypass_ = false;

  // Program detector state for the AUTO release (seconds spent above the
  // threshold; held while the envelope releases, reset once clearly below).
  std::vector<float> aboveTimeS_;

  // Feedback topology state: the compressed wet output of the previous sample,
  // fed back into the detector instead of the dry input.
  std::vector<float> fbOut_;

  // State
  EnvelopeFollower envelope_;
  // Second detector on the *delayed* audio: holds the gain while the delayed
  // tail of a loud passage is still playing, so the lookahead gain cannot
  // release back into that tail and swell the audio before the release curve.
  EnvelopeFollower delayedEnvelope_;
  // Thiran (allpass) interpolation: a fractional lookahead delay without the
  // low-pass filtering that linear interpolation would add to the signal.
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran>
      lookaheadDelay_;
  // A second delay line carrying the (sidechain) signal the main detector sees,
  // so the release-hold detector can watch exactly what the audio tail will
  // sound like at the output, including the HPF / feedback processing.
  juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Thiran>
      scDelay_;

  // One-pole high-pass used to strip low-frequency energy from the sidechain
  // before the envelope detector (DC-blocker with unity gain at Nyquist).
  struct OnePoleHighPass {
    float a = 0.0f;  // pole = exp(-2*pi*fc/sr)
    float z1 = 0.0f; // previous input
    float y1 = 0.0f; // previous output

    void setCutoff(double sr, float fc) {
      const double w =
          2.0 * juce::MathConstants<double>::pi * (double)fc / sr;
      a = (float)std::exp(-w);
    }
    float process(float x) {
      const float g = 0.5f * (1.0f + a);
      const float y = a * y1 + g * (x - z1);
      z1 = x;
      y1 = y;
      return y;
    }
    void reset() {
      z1 = 0.0f;
      y1 = 0.0f;
    }
  };
  std::vector<OnePoleHighPass> sidechainHpf_;
  bool hpfEnabled_ = false;

  // Smoothed parameter values
  juce::SmoothedValue<float> gainIn_, gainOut_, threshold_, ratio_, knee_, mix_,
      lookahead_;

  // Meter ballistics state
  float grLevel_ = 0.0f;
  float inputLevel_ = -120.0f;
  float outputLevel_ = -120.0f;

  int maxLookaheadSamples_ = 0;
  int numChannels_ = 0;

};