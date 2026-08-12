/*
  ==============================================================================

    GuiComponents.h
    Created: Aug 2026

    Custom-drawn widgets for the compressor UI:
      - Theme colours
      - SleekRotary  (painted knob)
      - VuMeter      (segmented meter, level or gain-reduction mode)
      - PowerSwitch  (bypass button)
      - Panel        (rounded section container with header)

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
namespace Theme {
inline const juce::Colour background = juce::Colour(0xFF0E1116);
inline const juce::Colour panel = juce::Colour(0xFF151A22);
inline const juce::Colour panelAlt = juce::Colour(0xFF191F2A);
inline const juce::Colour surface = juce::Colour(0xFF1F2531);
inline const juce::Colour surfaceBorder = juce::Colour(0xFF2C3343);
inline const juce::Colour text = juce::Colour(0xFFD3DAE6);
inline const juce::Colour textDim = juce::Colour(0xFF6E7889);
inline const juce::Colour textFaint = juce::Colour(0xFF4A525F);
inline const juce::Colour accent = juce::Colour(0xFF3DE8A3);
inline const juce::Colour accentDark = juce::Colour(0xFF15815B);
inline const juce::Colour amber = juce::Colour(0xFFFFB020);
inline const juce::Colour red = juce::Colour(0xFFFF5A5A);
inline const juce::Colour knobFace = juce::Colour(0xFF242B39);
inline const juce::Colour knobBezel = juce::Colour(0xFF39425A);

/** Convenience: a small bold font. */
inline juce::Font boldFont(float height) {
  auto f = juce::Font{juce::FontOptions(height)};
  f.setBold(true);
  return f;
}
}  // namespace Theme

//==============================================================================
/** A custom-painted rotary knob driven by a Slider::RotaryVerticalDrag. */
class SleekRotary : public juce::Slider {
 public:
  SleekRotary()
      : juce::Slider(juce::Slider::RotaryVerticalDrag,
                     juce::Slider::NoTextBox) {
    // stopAtEnd=true clamps the drag at min/max instead of wrapping past 100%.
    setRotaryParameters(juce::MathConstants<float>::pi * 0.85f,
                        juce::MathConstants<float>::pi * 2.15f, true);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setVelocityModeParameters(0.5f, 1, 0.01f, false);
  }

  /** Optional short label used to format the on-knob value readout. */
  void setValueFormatter(std::function<juce::String(float)> formatter) {
    fmt_ = std::move(formatter);
  }

  void paint(juce::Graphics &g) override;

 private:
  float valueToAngle() const noexcept {
    if (getMaximum() - getMinimum() <= 0)
      return 0.0f;
    const auto start = (double)juce::MathConstants<float>::pi * 0.85;
    const auto end = (double)juce::MathConstants<float>::pi * 2.15;
    return (float)juce::jmap((double)getValue(), (double)getMinimum(),
                             (double)getMaximum(), start, end);
  }

  std::function<juce::String(float)> fmt_;
};

//==============================================================================
/** A segmented vertical meter with peak hold. */
class VuMeter : public juce::Component {
 public:
  enum class Mode { level, gainReduction };

  VuMeter(Mode mode, const juce::String &title);
  ~VuMeter() override = default;

  void setDecibels(float db) { levelDb_ = db; }
  void setPeakHold(float db) { holdDb_ = db; }
  void paint(juce::Graphics &g) override;

 private:
  float levelToFraction() const noexcept;
  juce::Colour segmentColour(float fraction) const noexcept;

  Mode mode_;
  juce::String title_;
  float levelDb_ = -60.0f;
  float holdDb_ = -60.0f;
};

//==============================================================================
/** A small on/off power switch used for the master bypass. */
class PowerSwitch : public juce::ToggleButton {
 public:
  explicit PowerSwitch(const juce::String &caption);
  void paintButton(juce::Graphics &g, bool, bool) override;
};

//==============================================================================
/** A rounded panel container with an optional header title. */
class Panel : public juce::Component {
 public:
  explicit Panel(const juce::String &title = {});
  void paint(juce::Graphics &g) override;

 private:
  juce::String title_;
};