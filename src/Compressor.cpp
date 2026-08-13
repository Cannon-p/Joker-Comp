/*
  ==============================================================================

    Compressor.cpp
    Created: Aug 2026
    Author:  Joker_p

  ==============================================================================
*/

#include "Compressor.h"

#include <algorithm>
#include <cmath>

//==============================================================================
void EnvelopeFollower::prepare(double sr, int numChannels) {
  sr_ = sr > 0 ? sr : 44100.0;
  state_.clear();
  state_.resize((size_t)std::max(numChannels, 1));
}

void EnvelopeFollower::reset() {
  for (auto &s : state_)
    s.env = 0.0f;
}

float EnvelopeFollower::processSample(int channel, float absIn) {
  auto &s = state_[(size_t)channel];
  auto &env = s.env;

  if (absIn > env) {
    // One-pole exponential attack; the release-peak tracks the highest point
    // reached so the next release starts from the top of this attack.
    const float alpha = 1.0f - std::exp(-1.0f / (attackMs_ * 0.001f * sr_));
    env += alpha * (absIn - env);
    s.releasePeak = env;
  } else {
    // Log-shaped release: the time constant grows as the envelope falls, so
    // the decay is fast right after the transient and progressively slower as
    // it approaches the bottom - a log curve in dB instead of a straight line.
    const float progress = s.releasePeak > 1.0e-9f
                               ? 1.0f - env / s.releasePeak
                               : 1.0f;
    const float tau = releaseMs_ *
                      (1.0f + releaseLogDepth_ * progress);
    const float alpha = 1.0f - std::exp(-1.0f / (tau * 0.001f * sr_));
    env += alpha * (absIn - env);
  }

  return env;
}

//==============================================================================
void Compressor::prepare(double sampleRate, int numChannels) {
  sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
  numChannels_ = std::max(numChannels, 2);

  constexpr float maxLookaheadMs = 10.0f;  // must match UI range
  maxLookaheadSamples_ = (int)std::ceil(sampleRate_ * maxLookaheadMs / 1000.0);

  juce::dsp::ProcessSpec spec;
  spec.sampleRate = sampleRate_;
  spec.maximumBlockSize = 4096;
  spec.numChannels = (juce::uint32)numChannels_;

  envelope_.prepare(sampleRate_, numChannels_);
  delayedEnvelope_.prepare(sampleRate_, numChannels_);
  lookaheadDelay_.prepare(spec);
  lookaheadDelay_.setMaximumDelayInSamples(maxLookaheadSamples_);
  scDelay_.prepare(spec);
  scDelay_.setMaximumDelayInSamples(maxLookaheadSamples_);

  sidechainHpf_.clear();
  sidechainHpf_.resize(numChannels_);

  aboveTimeS_.clear();
  aboveTimeS_.resize((size_t)numChannels_, 0.0f);

  fbOut_.clear();
  fbOut_.resize((size_t)numChannels_, 0.0f);

  // Parameter smoothing ramps (~25 ms, 20 ms for look-ahead)
  constexpr double ramp = 0.025;
  gainIn_.reset(sampleRate_, ramp);
  gainOut_.reset(sampleRate_, ramp);
  threshold_.reset(sampleRate_, ramp);
  ratio_.reset(sampleRate_, ramp);
  knee_.reset(sampleRate_, ramp);
  mix_.reset(sampleRate_, ramp);
  lookahead_.reset(sampleRate_, 0.02);

  reset();
}

//==============================================================================
void Compressor::reset() {
  envelope_.reset();
  delayedEnvelope_.reset();
  lookaheadDelay_.reset();
  scDelay_.reset();

  hpfEnabled_ = targetSidechainHpfHz_ > 0.0f;
  for (auto &f : sidechainHpf_) {
    f.reset();
    if (hpfEnabled_)
      f.setCutoff(sampleRate_, targetSidechainHpfHz_);
  }

  const auto linGain = [](float db) {
    return juce::Decibels::decibelsToGain(db, -200.0f);
  };

  gainIn_.setCurrentAndTargetValue(
      std::min(linGain(targetInputGainDb_), 1000.0f));
  gainOut_.setCurrentAndTargetValue(
      std::min(linGain(targetOutputGainDb_), 1000.0f));
  threshold_.setCurrentAndTargetValue(targetThresholdDb_);
  ratio_.setCurrentAndTargetValue(std::max(1.0001f, targetRatio_));
  knee_.setCurrentAndTargetValue(juce::jlimit(0.0f, 24.0f, targetKneeDb_));
  mix_.setCurrentAndTargetValue(juce::jlimit(0.0f, 1.0f, targetMix_));
  lookahead_.setCurrentAndTargetValue(targetLookaheadMs_);

  grLevel_ = 0.0f;
  inputLevel_ = -120.0f;
  outputLevel_ = -120.0f;

  std::fill(aboveTimeS_.begin(), aboveTimeS_.end(), 0.0f);
  std::fill(fbOut_.begin(), fbOut_.end(), 0.0f);
}

//==============================================================================
void Compressor::setInputGainDb(float dB) { targetInputGainDb_ = dB; }
void Compressor::setOutputGainDb(float dB) { targetOutputGainDb_ = dB; }
void Compressor::setThresholdDb(float dB) { targetThresholdDb_ = dB; }
void Compressor::setRatio(float ratio) { targetRatio_ = std::max(1.0001f, ratio); }
void Compressor::setKneeDb(float db) {
  targetKneeDb_ = juce::jlimit(0.0f, 24.0f, db);
}
void Compressor::setSidechainHpfHz(float hz) {
  targetSidechainHpfHz_ = juce::jlimit(0.0f, 500.0f, hz);
}
void Compressor::setAttackMs(float ms) {
  targetAttackMs_ = std::max(0.01f, ms);
}
void Compressor::setReleaseMs(float ms) {
  targetReleaseMs_ = std::max(1.0f, ms);
}
void Compressor::setReleaseAutoEnabled(bool enabled) { releaseAuto_ = enabled; }
void Compressor::setLookaheadMs(float ms) {
  targetLookaheadMs_ = juce::jlimit(0.0f, 10.0f, ms);
}
void Compressor::setFeedbackMode(bool enabled) { feedbackMode_ = enabled; }
void Compressor::setMix(float mix) {
  targetMix_ = juce::jlimit(0.0f, 1.0f, mix);
}
void Compressor::setBypass(bool bypass) { bypass_ = bypass; }

//==============================================================================
void Compressor::updateSmoothed() {
  // Attack applies instantly to the envelope followers (cheap stores).
  envelope_.setAttackTime(targetAttackMs_);
  delayedEnvelope_.setAttackTime(targetAttackMs_);

  // Share the release ballistics between the main detector and the delayed
  // hold detector so both recover at the same rate.
  float releaseMs = targetReleaseMs_;
  float releaseLogDepth = 8.0f;
  if (releaseAuto_) {
    // Program-dependent release: short bursts above threshold (transients)
    // release fast; long sustained passages release slowly.
    float maxAbove = 0.0f;
    for (float t : aboveTimeS_)
      maxAbove = std::max(maxAbove, t);
    releaseMs = juce::jmap(juce::jlimit(0.03f, 0.8f, maxAbove), 0.03f, 0.8f,
                           60.0f, 900.0f);
    // The AUTO base time already provides the program character; keep the
    // release a plain exponential (no extra log tail) for predictability.
    releaseLogDepth = 0.0f;
  }
  envelope_.setReleaseTime(releaseMs);
  delayedEnvelope_.setReleaseTime(releaseMs);
  envelope_.setReleaseLogDepth(releaseLogDepth);
  delayedEnvelope_.setReleaseLogDepth(releaseLogDepth);

  gainIn_.setTargetValue(
      std::min(juce::Decibels::decibelsToGain(targetInputGainDb_, -200.0f),
               1000.0f));
  gainOut_.setTargetValue(
      std::min(juce::Decibels::decibelsToGain(targetOutputGainDb_, -200.0f),
               1000.0f));
  threshold_.setTargetValue(targetThresholdDb_);
  ratio_.setTargetValue(std::max(1.0001f, targetRatio_));
  knee_.setTargetValue(juce::jlimit(0.0f, 24.0f, targetKneeDb_));
  mix_.setTargetValue(juce::jlimit(0.0f, 1.0f, targetMix_));
  lookahead_.setTargetValue(targetLookaheadMs_);

  // Sidechain HPF cutoff tracks the parameter (no smoothing needed - the
  // filter's state is continuous so coefficient steps are inaudible here).
  const bool enable = targetSidechainHpfHz_ > 0.0f;
  if (enable != hpfEnabled_) {
    hpfEnabled_ = enable;
    for (auto &f : sidechainHpf_) {
      f.reset();
      if (enable)
        f.setCutoff(sampleRate_, targetSidechainHpfHz_);
    }
  } else if (enable) {
    for (auto &f : sidechainHpf_)
      f.setCutoff(sampleRate_, targetSidechainHpfHz_);
  }
}

//==============================================================================
namespace {

// Static gain-computer curve shared by the undelayed (lookahead) detector and
// the delayed hold detector. Returns GR in dB (<= 0, so the smaller value is
// the harder reduction).
float gainDbForEnvelope(float envDb, float threshDb, float ratioInv,
                        float kneeDb) {
  const float slope = 1.0f - ratioInv;  // >= 0 for ratio >= 1
  const float over = envDb - threshDb + 0.5f * kneeDb;
  if (over <= 0.0f)
    return 0.0f;
  if (kneeDb > 0.0f && over < kneeDb)
    // Quadratic blend region: [thresh - knee/2, thresh + knee/2].
    // GR = -slope * over^2 / (2*knee)  (always <= 0 dB, C1-continuous with the
    // hard-knee branch at over == knee).
    return -slope * (over * over) / (2.0f * kneeDb);
  return slope * (threshDb - envDb);
}

}  // namespace

//==============================================================================
void Compressor::process(juce::AudioBuffer<float> &buffer, int numChannels,
                         int numSamples) {
  if (numChannels <= 0 || numSamples <= 0)
    return;

  updateSmoothed();

  float blockGr = 0.0f;  // most negative GR in this block (<= 0 dB)
  float blockInPeak = 0.0f;
  float blockOutPeak = 0.0f;

  for (int s = 0; s < numSamples; ++s) {
    // Ramp the look-ahead delay per sample (20 ms) so knob moves are clean,
    // click-free and never smear the signal through a fractional delay sweep.
    const float delaySamples =
        lookahead_.getNextValue() * (float)(0.001 * sampleRate_);
    lookaheadDelay_.setDelay(delaySamples);
    scDelay_.setDelay(delaySamples);

    const float inGain = gainIn_.getNextValue();
    const float outGain = gainOut_.getNextValue();
    const float threshDb = threshold_.getNextValue();
    const float ratioInv = 1.0f / ratio_.getNextValue();
    const float kneeDb = knee_.getNextValue();
    const float mix = mix_.getNextValue();

    float grDb = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
      float x = buffer.getSample(ch, s);
      blockInPeak = std::max(blockInPeak, std::abs(x));

      // Feed-forward envelope, smoothed by the attack/release ballistics. The
      // optional sidechain HPF strips low-frequency energy so heavy lows don't
      // over-trigger the gain computer. In feedback mode the detector instead
      // watches the compressed output of the previous sample (classic feedback
      // topology - the gain reacts to what the compressor has already done).
      const float wetIn = x * inGain;
      float sc = (feedbackMode_ && (size_t)ch < fbOut_.size())
                     ? fbOut_[(size_t)ch]
                     : wetIn;
      if (hpfEnabled_ && (size_t)ch < sidechainHpf_.size())
        sc = sidechainHpf_[(size_t)ch].process(sc);
      const float env = envelope_.processSample(ch, std::abs(sc));
      const float envDb = juce::Decibels::gainToDecibels(env, -200.0f);

      // AUTO release program detector: count time spent above threshold; hold
      // it while the envelope is still releasing back through the threshold,
      // reset once it is clearly below.
      if (releaseAuto_) {
        constexpr float holdDb = 6.0f;
        auto &at = aboveTimeS_[(size_t)ch];
        if (envDb > threshDb)
          at = std::min(at + (float)(1.0 / sampleRate_), 0.8f);
        else if (envDb <= threshDb - holdDb)
          at = 0.0f;
        // else: hold the value while releasing through the hold band
      }

      // Static curve with an optional soft knee. GR is <= 0 dB.
      float gainLin = 1.0f;
      grDb = 0.0f;
      if (!bypass_) {
        grDb = gainDbForEnvelope(envDb, threshDb, ratioInv, kneeDb);
        gainLin = juce::Decibels::decibelsToGain(grDb, -200.0f);
      }

      // Look-ahead: `delayed` is `lookahead` ms old while gainLin was computed
      // from the *future* (undelayed) input, giving the gain control that extra
      // reaction time before the transient reaches the output.
      lookaheadDelay_.pushSample(ch, wetIn);
      const float delayed = lookaheadDelay_.popSample(ch);

      // The sidechain, delayed by the same amount: the signal the main detector
      // reacts to, seen at the time the captured audio will reach the output.
      scDelay_.pushSample(ch, sc);
      const float scDelayed = scDelay_.popSample(ch);

      // Release-bump prevention (feed-forward only): when the input cuts out,
      // the undelayed detector releases straight away while the delayed audio
      // tail is still playing, so the gain would recover back into it and
      // audibly swell before the release curve. A second detector on the
      // delayed sidechain holds the gain until the tail is actually gone;
      // taking the smaller (more reduced) of the two gains keeps the lookahead
      // attack advantage without letting the tail pop back up. With zero
      // lookahead the delayed sidechain equals the undelayed one, so this
      // reduces to the plain compressor. Feedback mode already tracks the
      // delayed output, so its character is left untouched.
      float gainFinal = gainLin;
      if (!bypass_ && !feedbackMode_) {
        const float envDbD = juce::Decibels::gainToDecibels(
            delayedEnvelope_.processSample(ch, std::abs(scDelayed)), -200.0f);
        const float grDbHold =
            gainDbForEnvelope(envDbD, threshDb, ratioInv, kneeDb);
        const float gainHold =
            juce::Decibels::decibelsToGain(grDbHold, -200.0f);
        gainFinal = std::min(gainLin, gainHold);
        grDb = std::min(grDb, grDbHold);  // meter shows the applied reduction
      }

      float wetOut = delayed;
      wetOut *= gainFinal;

      // Feedback tap: hand the compressed wet signal back to the detector for
      // the next sample (only read in feedback mode).
      if ((size_t)ch < fbOut_.size())
        fbOut_[(size_t)ch] = wetOut;

      // Dry/wet mix - both paths come from the same delay line, so dry and wet
      // stay in perfect phase alignment. In bypass mode the signal is fully wet
      // with unity gain (the delay keeps running for glitch-free toggling).
      const float mixFrac = bypass_ ? 1.0f : mix;
      const float dryFrac = 1.0f - mixFrac;
      float out = mixFrac * wetOut + dryFrac * delayed;
      out *= outGain;

      buffer.setSample(ch, s, out);
      blockOutPeak = std::max(blockOutPeak, std::abs(out));
    }

    // GR is <= 0 dB, so the block value is the most *negative* sample.
    blockGr = std::min(blockGr, grDb);
  }

  // ---- Meter ballistics (fast attack, slow release) ----
  const float inDb =
      juce::Decibels::gainToDecibels(std::max(blockInPeak, 1.0e-9f), -120.0f);
  const float outDb =
      juce::Decibels::gainToDecibels(std::max(blockOutPeak, 1.0e-9f), -120.0f);

  // Per-block smoothing coefficient converted from a per-sample time constant.
  const auto blockCoeff = [numSamples, sr = sampleRate_](float tauSeconds) {
    const double alpha = 1.0 - std::exp(-1.0 / (double)(tauSeconds * sr));
    return (float)(1.0 - std::pow(1.0 - alpha, (double)numSamples));
  };

  const float fastCoeff = blockCoeff(0.005f);     // near-instant upward
  const float meterRelease = blockCoeff(0.30f);   // level decay

  // Gain reduction: attack snaps to the new (deeper) value; release tracks the
  // block value closely with a short fixed time constant so the *envelope's*
  // log-shaped fast-then-slow curve is what the meter displays, without the
  // meter adding its own slow character on top.
  const float grAlpha = blockGr < grLevel_ ? fastCoeff : blockCoeff(0.06f);
  grLevel_ += grAlpha * (blockGr - grLevel_);
  grLevel_ = juce::jlimit(-18.0f, 0.0f, grLevel_);

  // Input / output level meters: rise fast, fall with a release tail.
  inputLevel_ += (inDb > inputLevel_) ? fastCoeff * (inDb - inputLevel_)
                                      : meterRelease * (inDb - inputLevel_);
  outputLevel_ += (outDb > outputLevel_) ? fastCoeff * (outDb - outputLevel_)
                                         : meterRelease * (outDb - outputLevel_);
}