/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginEditor.h"

#include "PluginProcessor.h"

//==============================================================================
CompressorAudioProcessorEditor::CompressorAudioProcessorEditor(
    CompressorAudioProcessor &p)
    : AudioProcessorEditor(&p),
      bypassAttachment_(p.apvts, "bypass", bypassSwitch_),
      lookaheadAttachment_(p.apvts, "lookaheadOn", lookaheadSwitch_),
      feedbackAttachment_(p.apvts, "feedback", feedbackSwitch_),
      audioProcessor(p) {
  // Make sure that before the constructor has finished, you've set the
  // editor's size to whatever you need it to be.
  setSize(820, 560);
  startTimerHz(30);

  addAndMakeVisible(mainPanel_);
  addAndMakeVisible(meterPanel_);
  addAndMakeVisible(modelPanel_);
  addAndMakeVisible(grMeter_);
  addAndMakeVisible(bypassSwitch_);
  addAndMakeVisible(lookaheadSwitch_);
  addAndMakeVisible(feedbackSwitch_);

  // Analog-model select strip: exclusive mode buttons. The analog engines are
  // not wired into the DSP yet, so the selected model is UI state for now;
  // DIGITAL is switched on below as the default (the engine developed so far).
  addModelButton("DIGITAL");
  addModelButton("2A");
  addModelButton("3A");
  addModelButton("1176");
  addModelButton("2500");
  addModelButton("160");
  addModelButton("609");
  modelButtons_.getFirst()->setToggleState(true, juce::dontSendNotification);

  // Row 1: Threshold, Ratio, Lookahead, Attack, Release
  addKnob("threshold", "THRESHOLD",
          [](float v) { return juce::String(v, 1) + " dB"; });
  addKnob("ratio", "RATIO", [](float v) {
    return juce::String::formatted("%.1f : 1", v);
  });
  lookaheadKnob_ = addKnob("lookahead", "LOOKAHEAD",
                           [](float v) { return juce::String(v, 1) + " ms"; });
  addKnob("attack", "ATTACK",
          [](float v) { return juce::String(v, 1) + " ms"; });
  addKnob("release", "RELEASE",
          [](float v) { return juce::String(v, 0) + " ms"; });

  // Row 2: Input, Knee, HPF, Mix, Output
  addKnob("input", "INPUT",
          [](float v) { return juce::String(v, 1) + " dB"; });
  addKnob("knee", "KNEE",
          [](float v) { return juce::String(v, 0) + " dB"; });
  addKnob("hpf", "HPF", [](float v) {
    return v <= 0.0f ? "OFF" : juce::String(juce::roundToInt(v)) + " Hz";
  });
  addKnob("mix", "MIX", [](float v) {
    return juce::String(juce::roundToInt(v * 100.0f)) + " %";
  });
  addKnob("output", "OUTPUT",
          [](float v) { return juce::String(v, 1) + " dB"; });

  // Lookahead switch freezes the knob while disabled (DSP also ignores it).
  lookaheadSwitch_.onStateChange = [this]() { updateLookaheadKnobState(); };
  updateLookaheadKnobState();

  // The initial setSize() call in the constructor fired resized() before the
  // knobs existed, so lay the whole UI out again now that everything exists.
  resized();
}

CompressorAudioProcessorEditor::~CompressorAudioProcessorEditor() {}

//==============================================================================
SleekRotary *CompressorAudioProcessorEditor::addKnob(
    const char *paramId, const juce::String &caption,
    std::function<juce::String(float)> formatter) {
  auto *param = audioProcessor.apvts.getParameter(paramId);
  if (param == nullptr)
    return nullptr;

  auto *knob = new SleekRotary();

  if (auto *f = dynamic_cast<juce::AudioParameterFloat *>(param)) {
    const auto &range = f->getNormalisableRange();
    knob->setRange((double)range.start, (double)range.end,
                   (double)range.interval);
    knob->setSkewFactor(range.skew);
    knob->setValue(f->get());
    knob->setValueFormatter(std::move(formatter));
  } else if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(param)) {
    // Stepped choices: knob maps to the selected index (0..N-1).
    const int n = c->choices.size();
    knob->setRange(0.0, (double)(n - 1), 1.0);
    knob->setValue((double)c->getIndex());
    knob->setValueFormatter([c](float idx) {
      const int i = juce::roundToInt(idx);
      return (i >= 0 && i < c->choices.size()) ? c->choices[i]
                                               : juce::String();
    });
  } else {
    delete knob;
    return nullptr;
  }

  addAndMakeVisible(knob);
  knobs_.add(knob);

  auto *label = new juce::Label();
  label->setText(caption, juce::dontSendNotification);
  label->setJustificationType(juce::Justification::centred);
  label->setFont(Theme::boldFont(13.0f));
  label->setColour(juce::Label::textColourId, Theme::textDim);
  addAndMakeVisible(label);
  knobLabels_.add(label);

  sliderAttachments_.add(
      std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
          audioProcessor.apvts, paramId, *knob));

  return knob;
}

//==============================================================================
void CompressorAudioProcessorEditor::updateLookaheadKnobState() {
  if (lookaheadKnob_ == nullptr)
    return;
  const bool on = lookaheadSwitch_.getToggleState();
  lookaheadKnob_->setInterceptsMouseClicks(on, on);
  lookaheadKnob_->setAlpha(on ? 1.0f : 0.35f);
  lookaheadKnob_->repaint();
}

//==============================================================================
void CompressorAudioProcessorEditor::addModelButton(
    const juce::String &caption) {
  auto *button = new PowerSwitch(caption);
  // Exclusive radio behaviour: clicking a model lights that button and dims
  // all the others. The strips aren't wired to the DSP yet.
  button->onClick = [this, button]() {
    for (auto *b : modelButtons_)
      b->setToggleState(b == button, juce::dontSendNotification);
  };
  addAndMakeVisible(button);
  modelButtons_.add(button);
}

//==============================================================================
void CompressorAudioProcessorEditor::paint(juce::Graphics &g) {
  // ---- Background ----
  g.fillAll(Theme::background);
  g.setGradientFill(juce::ColourGradient(
      juce::Colour(0xFF16202C), 0.0f, 0.0f, Theme::background, 0.0f,
      (float)getHeight(), false));
  g.fillRect(getLocalBounds());

  // ---- Header ----
  g.setFont(Theme::boldFont(27.0f));
  g.setColour(Theme::accent);
  g.drawText("JOKER", 22, 14, 110, 34, juce::Justification::centredLeft);
  g.setColour(Theme::text);
  g.drawText("COMP", 132, 14, 160, 34, juce::Justification::centredLeft);

  // Header accent line
  g.setGradientFill(juce::ColourGradient(
      Theme::accentDark, 24.0f, 66.0f, juce::Colour(0x00000000),
      (float)getWidth(), 66.0f, false));
  g.fillRect(24, 66, getWidth() - 48, 2);
}

//==============================================================================
void CompressorAudioProcessorEditor::resized() {
  const int w = getWidth();
  const int h = getHeight();

  bypassSwitch_.setBounds(w - 138, 20, 118, 30);

  meterPanel_.setBounds(12, 84, 150, 390);
  grMeter_.setBounds(18, 104, 114, 350);

  mainPanel_.setBounds(174, 84, w - 186, 390);

  // Feed-forward/feedback switch to the right of the green "Compressor" title
  // (drawn at panel x+14, y+8); sits in the header band above the knob grid.
  feedbackSwitch_.setBounds(mainPanel_.getX() + 96, mainPanel_.getY() + 2, 130,
                            26);

  // Knob grid: 5 columns x 2 rows (9 knobs)
  const int panelX = 174, panelY = 84, panelW = w - 186, panelH = 390;
  const int contentX = panelX + 24;
  const int contentY = panelY + 38;
  const int contentW = panelW - 48;
  const int contentH = panelH - 58;

  constexpr int cols = 5;
  constexpr int rows = 2;
  constexpr int knobW = 84;
  constexpr int knobH = 96;

  const int cellW = contentW / cols;
  const int cellH = contentH / rows;

  for (int i = 0; i < knobs_.size(); ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int cx = contentX + col * cellW + (cellW - knobW) / 2;
    const int cy = contentY + row * cellH + (cellH - knobH) / 2;

    knobs_[i]->setBounds(cx, cy, knobW, knobH);
    knobLabels_[i]->setBounds(contentX + col * cellW, cy - 20, cellW, 16);
  }

  // Lookahead master switch directly below the LOOKAHEAD knob (col 2, row 0),
  // centred on the same column so it reads as belonging to that knob.
  constexpr int switchW = 100;
  constexpr int switchH = 24;
  const int laX = contentX + 2 * cellW + (cellW - switchW) / 2;
  const int laY = contentY + (cellH - knobH) / 2 + knobH + 7;
  lookaheadSwitch_.setBounds(laX, laY, switchW, switchH);

  modelPanel_.setBounds(12, 486, w - 24, 62);

  // Model select strip: sits to the right of the "Analog Modeling" title and
  // flows left-to-right. Buttons are compact so the strip fits the 7 current
  // models and still leaves room on the right for more to be added later.
  constexpr int btnW = 72;
  constexpr int btnH = 30;
  constexpr int gap = 12;
  int bx = modelPanel_.getX() + 180;
  const int by = modelPanel_.getY() + 18;
  for (int i = 0; i < modelButtons_.size(); ++i) {
    modelButtons_[i]->setBounds(bx, by, btnW, btnH);
    bx += btnW + gap;
  }
}

//==============================================================================
void CompressorAudioProcessorEditor::timerCallback() {
  const float gr = audioProcessor.getGainReductionDb();

  // Peak holds decay slowly for a classic meter feel
  grPeakHold_ = juce::jmax(gr, grPeakHold_ - 0.4f);

  grMeter_.setDecibels(gr);
  grMeter_.setPeakHold(grPeakHold_);

  grMeter_.repaint();
}