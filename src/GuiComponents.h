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

#include <functional>
#include <memory>

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
/** Small floating popup that lets the user type an exact value into a knob.
    Typed values are snapped to the knob's step size and clamped to its range. */
class KnobValueEditor;  // fwd decl (owned by SleekRotary)

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

  /** Fine-grained mouse wheel: one notch moves a small, parameter-aware step
      (0.5% of the range, at least one interval) instead of the default ~15%
      jump. Values are snapped/clamped by Slider::setValue. */
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override;

  /** Double-click opens a numeric entry popup instead of resetting to the
      default value. */
  void mouseDown(const juce::MouseEvent &e) override;

  /** Swallows the framework's double-click callback so the base Slider can
      never restore the default value on a double-click. */
  void mouseDoubleClick(const juce::MouseEvent &) override;

 private:
  void showValueEditor();

  float valueToAngle() const noexcept {
    if (getMaximum() - getMinimum() <= 0)
      return 0.0f;
    const auto start = (double)juce::MathConstants<float>::pi * 0.85;
    const auto end = (double)juce::MathConstants<float>::pi * 2.15;
    return (float)juce::jmap((double)getValue(), (double)getMinimum(),
                             (double)getMaximum(), start, end);
  }

  std::function<juce::String(float)> fmt_;
  double wheelAccum_ = 0.0;
  std::unique_ptr<KnobValueEditor> valueEditor_;
};

//==============================================================================
/** A small desktop popup with a single-line numeric editor for a knob. The
    value is parsed, snapped to the slider's interval and clamped to its range
    on Enter. Created and released by SleekRotary. */
class KnobValueEditor : public juce::Component,
                        public juce::TextEditor::Listener {
 public:
  KnobValueEditor(juce::Slider &slider, const juce::String &caption);
  ~KnobValueEditor() override = default;

  /** Positions the popup near the knob and grabs keyboard focus. */
  void show();

  /** Parses text, snaps it onto the step grid and clamps to the range, then
      applies it to the slider. Exposed so tests can drive it directly. */
  void applyText(const juce::String &text);

  void paint(juce::Graphics &g) override;
  void resized() override;

  void textEditorReturnKeyPressed(juce::TextEditor &) override;
  void textEditorEscapeKeyPressed(juce::TextEditor &) override;
  void textEditorFocusLost(juce::TextEditor &) override;

  /** The owning knob sets this to release its handle once the popup closes. */
  std::function<void()> onClose;

 private:
  void close(bool apply);

  juce::Slider &slider_;
  juce::Label caption_;
  juce::TextEditor editor_;
  bool closing_ = false;
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