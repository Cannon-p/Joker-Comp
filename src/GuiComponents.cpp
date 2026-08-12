/*
  ==============================================================================

    GuiComponents.cpp
    Created: Aug 2026

  ==============================================================================
*/

#include "GuiComponents.h"

#include <cmath>

//==============================================================================
void SleekRotary::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  const float knobD =
      juce::jmin(bounds.getWidth() - 12.0f, bounds.getHeight() - 30.0f);
  const float r = knobD * 0.5f;
  const float cx = bounds.getCentreX();
  const float centerY = bounds.getBottom() - 8.0f - r;

  const float curAngle = valueToAngle();

  // ---- Knob body ----
  g.setColour(Theme::knobBezel);
  g.fillEllipse(cx - r, centerY - r, knobD, knobD);
  g.setGradientFill(juce::ColourGradient(
      Theme::knobFace.brighter(0.35f), cx - r * 0.6f, centerY - r * 0.6f,
      Theme::knobFace.darker(0.45f), cx + r * 0.6f, centerY + r * 0.6f,
      false));
  g.fillEllipse(cx - r + 2.0f, centerY - r + 2.0f, knobD - 4.0f,
                knobD - 4.0f);

  // ---- Pointer ----
  const float px = cx + std::cos(curAngle) * r * 0.7f;
  const float py = centerY + std::sin(curAngle) * r * 0.7f;
  g.setColour(Theme::text);
  g.drawLine(cx, centerY, px, py, 2.5f);
  g.setColour(Theme::accent);
  g.fillEllipse(cx - 3.25f, centerY - 3.25f, 6.5f, 6.5f);

  // ---- Value readout ----
  const float val = getValue();
  juce::String text;
  if (fmt_)
    text = fmt_(val);
  else
    text = juce::String(val, 1);

  g.setColour(Theme::text);
  g.setFont(Theme::boldFont(14.0f));
  g.drawFittedText(text, juce::Rectangle<float>(bounds.getX(),
                                                bounds.getBottom() - 22.0f,
                                                bounds.getWidth(), 20.0f)
                                .toNearestInt(),
                   juce::Justification::centred, 1);
}

//==============================================================================
void SleekRotary::mouseWheelMove(const juce::MouseEvent &e,
                                 const juce::MouseWheelDetails &wheel) {
  juce::ignoreUnused(e);
  if (!isEnabled())
    return;

  double delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
                     ? -wheel.deltaX
                     : wheel.deltaY;
  if (wheel.isReversed)
    delta = -delta;
  if (delta == 0.0)
    return;

  // Small, parameter-aware step: at least one interval, otherwise 0.5% of the
  // full range - fine control. setValue snaps to the interval and clamps to
  // the range automatically.
  const double step =
      std::max(getInterval(), (getMaximum() - getMinimum()) * 0.005);
  if (step <= 0.0)
    return;

  // JUCE does not normalise wheel deltas across platforms: on Windows a full
  // notch arrives as deltaY ~= 120 / 512 ~= 0.234, while macOS delivers ~1.0.
  // Accumulate the raw deltas and apply one fine step per completed notch, so
  // a single notch on a normal mouse is exactly one fine step, and smooth
  // mice / trackpads (which emit many small deltas per notch) add up to a step
  // instead of every fragment being swallowed by the step-grid snapping.
  constexpr double kNotchDelta = 120.0 / 512.0;  // JUCE Windows wheel scale

  wheelAccum_ += delta;

  const double notches = wheelAccum_ / kNotchDelta;
  const double whole = std::floor(std::abs(notches));
  if (whole < 1.0)
    return;

  const double dir = notches < 0.0 ? -1.0 : 1.0;
  wheelAccum_ -= dir * whole * kNotchDelta;

  setValue(getValue() + dir * whole * step, juce::sendNotificationSync);
}

void SleekRotary::mouseDoubleClick(const juce::MouseEvent &) {
  // Intentionally empty: the framework dispatches this callback on a
  // double-click, and the base Slider would reset the value to its default
  // here. The value-entry popup is already opened from mouseDown().
}

void SleekRotary::mouseDown(const juce::MouseEvent &e) {
  if (isEnabled() && e.mods.isLeftButtonDown() && e.getNumberOfClicks() >= 2) {
    showValueEditor();
    return;  // don't start a drag or reset the value
  }
  juce::Slider::mouseDown(e);
}

void SleekRotary::showValueEditor() {
  if (valueEditor_ != nullptr)
    valueEditor_.reset();

  juce::String caption;
  if (fmt_)
    caption = fmt_((float)getValue());
  else
    caption = juce::String(getValue(), 2);

  auto editor = std::make_unique<KnobValueEditor>(*this, caption);
  auto *raw = editor.get();
  raw->onClose = [safe = juce::Component::SafePointer<SleekRotary>(this)]() {
    // Delete asynchronously so the popup is never destroyed from inside its
    // own message handler.
    if (safe != nullptr)
      juce::MessageManager::callAsync(
          [safe]() { if (safe != nullptr) safe->valueEditor_.reset(); });
  };
  valueEditor_ = std::move(editor);
  raw->show();
}

//==============================================================================
KnobValueEditor::KnobValueEditor(juce::Slider &slider,
                                 const juce::String &caption)
    : slider_(slider) {
  caption_.setText(caption, juce::dontSendNotification);
  caption_.setFont(Theme::boldFont(11.0f));
  caption_.setColour(juce::Label::textColourId, Theme::textDim);
  caption_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(caption_);

  editor_.setFont(Theme::boldFont(16.0f));
  editor_.setColour(juce::TextEditor::textColourId, Theme::text);
  editor_.setColour(juce::TextEditor::backgroundColourId, Theme::surface);
  editor_.setColour(juce::TextEditor::outlineColourId, Theme::accentDark);
  editor_.setColour(juce::TextEditor::focusedOutlineColourId, Theme::accent);
  editor_.setJustification(juce::Justification::centred);
  editor_.setInputRestrictions(16, "0123456789.-");
  editor_.setReturnKeyStartsNewLine(false);
  editor_.addListener(this);
  editor_.setText(juce::String(slider_.getValue(), 4),
                  juce::dontSendNotification);
  addAndMakeVisible(editor_);
}

void KnobValueEditor::show() {
  setSize(160, 60);
  addToDesktop(juce::ComponentPeer::windowIsTemporary, nullptr);

  const auto display =
      juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
  const auto pos = slider_.getScreenPosition();
  const int x = juce::jlimit(8, display.getWidth() - 170, pos.x);
  const int y = pos.y + slider_.getHeight() + 6;
  setBounds(x, y, 160, 60);
  setVisible(true);
  toFront(true);
  editor_.grabKeyboardFocus();
  editor_.selectAll();
}

void KnobValueEditor::applyText(const juce::String &text) {
  const double mn = slider_.getMinimum();
  const double mx = slider_.getMaximum();
  const double step = slider_.getInterval();
  double value = text.getDoubleValue();

  // Snap onto the step grid (relative to the range start) so e.g. a 1 dB-step
  // knob snaps to whole dB values, then clamp to the allowed range. setValue
  // also clamps, but this keeps the displayed result predictable.
  if (step > 0.0)
    value = mn + std::round((value - mn) / step) * step;
  value = juce::jlimit(mn, mx, value);

  slider_.setValue(value, juce::sendNotificationSync);
}

void KnobValueEditor::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();
  g.setColour(Theme::surface);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(Theme::accentDark);
  g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
}

void KnobValueEditor::resized() {
  auto b = getLocalBounds();
  caption_.setBounds(b.removeFromTop(20));
  editor_.setBounds(b.reduced(8, 4));
}

void KnobValueEditor::textEditorReturnKeyPressed(juce::TextEditor &) {
  close(true);
}
void KnobValueEditor::textEditorEscapeKeyPressed(juce::TextEditor &) {
  close(false);
}
void KnobValueEditor::textEditorFocusLost(juce::TextEditor &) { close(false); }

void KnobValueEditor::close(bool apply) {
  if (closing_)
    return;
  closing_ = true;

  if (apply)
    applyText(editor_.getText());

  removeFromDesktop();
  if (onClose)
    onClose();
}

//==============================================================================
VuMeter::VuMeter(Mode mode, const juce::String &title)
    : mode_(mode), title_(title) {
  levelDb_ = (mode == Mode::gainReduction) ? 0.0f : -60.0f;
  holdDb_ = levelDb_;
}

float VuMeter::levelToFraction() const noexcept {
  if (mode_ == Mode::gainReduction)
    return juce::jlimit(0.0f, 1.0f, -levelDb_ / 18.0f);
  return juce::jlimit(0.0f, 1.0f, (levelDb_ + 60.0f) / 60.0f);
}

juce::Colour VuMeter::segmentColour(float fraction) const noexcept {
  if (fraction < 0.62f)
    return Theme::accent;
  if (fraction < 0.82f)
    return Theme::amber;
  return Theme::red;
}

void VuMeter::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  // Title
  g.setColour(Theme::textDim);
  g.setFont(Theme::boldFont(12.0f));
  g.drawFittedText(title_, juce::Rectangle<float>(bounds.getX(), bounds.getY(),
                                                   bounds.getWidth(), 18.0f)
                               .toNearestInt(),
                   juce::Justification::centred, 1);

  // Meter body
  const float bodyX = bounds.getX() + 2.0f;
  const float bodyW = bounds.getWidth() - 4.0f;
  const float bodyY = bounds.getY() + 22.0f;
  const float bodyH = bounds.getHeight() - 34.0f;

  const float barX = bodyX + 12.0f;
  const float barW = bodyW - 24.0f;

  // Scale labels
  constexpr int numTicks = 5;
  g.setFont(juce::FontOptions(11.0f));
  for (int i = 0; i < numTicks; ++i) {
    const float t = (float)i / (float)(numTicks - 1);
    const float y = bodyY + bodyH - t * bodyH;
    g.setColour(Theme::textFaint);
    g.drawHorizontalLine(juce::roundToInt(y), barX - 3.0f, barX - 8.0f);

    // Gain-reduction scale reads the *amount* of reduction as a positive
    // number: 0 dB at the top, 18 dB at the bottom (small -> large downward).
    const float db = (mode_ == Mode::gainReduction) ? 18.0f * (1.0f - t)
                                                    : -60.0f * (1.0f - t);
    g.setColour(Theme::textDim);
    g.drawSingleLineText(juce::String((int)db),
                         juce::roundToInt(barX - 9.0f), juce::roundToInt(y));
  }

  // Segmented LED bar. The gain-reduction meter lights *descend* from the top
  // as reduction increases (0 dB at the top, 24 dB at the bottom); the level
  // meters rise from the bottom up.
  constexpr int numSegs = 28;
  const float gap = 1.5f;
  const float segH = (bodyH - gap * (numSegs - 1)) / (float)numSegs;
  const float frac = levelToFraction();
  const int lit = (int)std::lround(frac * (float)numSegs);
  const bool grMode = (mode_ == Mode::gainReduction);

  for (int i = 0; i < numSegs; ++i) {
    const float y =
        grMode ? bodyY + (float)i * (segH + gap)
               : bodyY + bodyH - ((float)i + 1.0f) * (segH + gap);
    const float segFrac = (float)i / (float)(numSegs - 1);
    juce::Colour c;
    if (i < lit) {
      c = segmentColour(segFrac);
    } else {
      c = Theme::panelAlt;
    }
    g.setColour(c);
    g.fillRoundedRectangle(barX, y, barW, segH, 2.0f);
  }

  // Peak hold marker
  const float holdFrac = (mode_ == Mode::gainReduction)
                             ? juce::jlimit(0.0f, 1.0f, -holdDb_ / 18.0f)
                             : juce::jlimit(0.0f, 1.0f,
                                            (holdDb_ + 60.0f) / 60.0f);
  const float holdY =
      grMode ? bodyY + holdFrac * bodyH : bodyY + bodyH - holdFrac * bodyH;
  g.setColour(Theme::text);
  g.fillRect(barX - 2.0f, holdY - 1.0f, barW + 4.0f, 2.0f);
}

//==============================================================================
PowerSwitch::PowerSwitch(const juce::String &caption) {
  setButtonText(caption);
  setClickingTogglesState(true);
  setSize(110, 30);
}

void PowerSwitch::paintButton(juce::Graphics &g, bool, bool) {
  const auto bounds = getLocalBounds().toFloat();

  const bool on = getToggleState();

  g.setColour(Theme::panelAlt);
  g.fillRoundedRectangle(bounds, 6.0f);
  g.setColour(on ? Theme::accentDark : Theme::surfaceBorder);
  g.drawRoundedRectangle(bounds, 6.0f, 1.5f);

  const float dotR = 4.5f;
  const float dotX = on ? bounds.getWidth() - 22.0f : 22.0f;
  const float dotY = bounds.getCentreY();

  g.setColour(on ? Theme::accent : Theme::textDim);
  g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

  g.setColour(on ? Theme::text : Theme::textDim);
  g.setFont(Theme::boldFont(13.0f));
  g.drawFittedText(getButtonText(), bounds.toNearestInt(),
                   juce::Justification::centred, 1);
}

//==============================================================================
Panel::Panel(const juce::String &title) : title_(title) {}

void Panel::paint(juce::Graphics &g) {
  const auto bounds = getLocalBounds().toFloat();

  g.setColour(Theme::panel);
  g.fillRoundedRectangle(bounds, 10.0f);
  g.setColour(Theme::surfaceBorder);
  g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

  if (title_.isNotEmpty()) {
    g.setColour(Theme::accent);
    g.setFont(Theme::boldFont(14.0f));
    g.drawText(title_, juce::Rectangle<float>(bounds.getX() + 14.0f,
                                               bounds.getY() + 8.0f,
                                               bounds.getWidth() - 28.0f,
                                               14.0f),
               juce::Justification::topLeft);
  }
}