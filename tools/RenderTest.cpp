/*
  ==============================================================================

    RenderTest.cpp
    A tiny harness that renders the plugin editor off-screen into a PNG so the
    GUI can be verified without launching a DAW.

  ==============================================================================
*/

#include <JuceHeader.h>

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

//==============================================================================
static int countCloseTo(const juce::Image &img, juce::Colour target,
                        int tolerance) {
  int count = 0;
  const int w = img.getWidth();
  const int h = img.getHeight();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      auto c = img.getPixelAt(x, y);
      int dr = std::abs(c.getRed() - target.getRed());
      int dg = std::abs(c.getGreen() - target.getGreen());
      int db = std::abs(c.getBlue() - target.getBlue());
      if (dr <= tolerance && dg <= tolerance && db <= tolerance)
        ++count;
    }
  }
  return count;
}

//==============================================================================
int main(int argc, char **argv) {
  juce::ignoreUnused(argc, argv);
  juce::ScopedJuceInitialiser_GUI initialiser;

  CompressorAudioProcessor processor;
  processor.prepareToPlay(44100.0, 512);

  CompressorAudioProcessorEditor editor(processor);
  editor.setSize(820, 560);
  editor.resized();

  std::cout << "Children: " << editor.getNumChildComponents() << std::endl;

  // ---- Feed a loud sine through the compressor and check the meters react ----
  const int numSamples = 512;
  juce::AudioBuffer<float> block(2, numSamples);
  juce::AudioBuffer<float> sine(2, numSamples);
  for (int i = 0; i < numSamples; ++i) {
    const float v =
        0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f *
                         (float)i / 44100.0f);  // -6 dBFS sine
    sine.setSample(0, i, v);
    sine.setSample(1, i, v);
  }

  auto *threshParam =
      dynamic_cast<juce::AudioParameterFloat *>(processor.apvts.getParameter("threshold"));
  auto *ratioParam =
      dynamic_cast<juce::AudioParameterChoice *>(processor.apvts.getParameter("ratio"));
  auto *bypassParam =
      dynamic_cast<juce::AudioParameterBool *>(processor.apvts.getParameter("bypass"));
  if (threshParam != nullptr)
    *threshParam = -20.0f;  // default is 0 dB now; lower it for the tests
  std::cout << "threshold=" << (threshParam ? threshParam->get() : -999.0f)
            << " ratio="
            << (ratioParam ? ratioParam->getCurrentChoiceName() : "n/a")
            << std::endl;

  auto runBlocks = [&](int count) {
    for (int r = 0; r < count; ++r) {
      block.copyFrom(0, 0, sine, 0, 0, numSamples);
      block.copyFrom(1, 0, sine, 1, 0, numSamples);
      juce::MidiBuffer midi;
      processor.processBlock(block, midi);
    }
  };

  runBlocks(30);
  std::cout << "[compressing] GR=" << processor.getGainReductionDb()
            << " IN=" << processor.getInputLevelDb()
            << " OUT=" << processor.getOutputLevelDb() << std::endl;

  if (bypassParam != nullptr) {
    bypassParam->setValueNotifyingHost(1.0f);
    runBlocks(5);
    std::cout << "[bypassed]    GR=" << processor.getGainReductionDb()
              << " OUT=" << processor.getOutputLevelDb() << std::endl;
    bypassParam->setValueNotifyingHost(0.0f);
    runBlocks(5);
    std::cout << "[back on]     GR=" << processor.getGainReductionDb()
              << " OUT=" << processor.getOutputLevelDb() << std::endl;
  }

  // ---- Soft-knee: near-threshold input must be reduced smoothly, never boosted ----
  auto *kneeParam =
      dynamic_cast<juce::AudioParameterFloat *>(processor.apvts.getParameter("knee"));
  juce::AudioBuffer<float> nearThresh(2, numSamples);
  for (int i = 0; i < numSamples; ++i) {
    const float v = 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi *
                                    440.0f * (float)i / 44100.0f);  // -14 dBFS
    nearThresh.setSample(0, i, v);
    nearThresh.setSample(1, i, v);
  }
  auto runNearThresh = [&](int count) {
    for (int r = 0; r < count; ++r) {
      block.copyFrom(0, 0, nearThresh, 0, 0, numSamples);
      block.copyFrom(1, 0, nearThresh, 1, 0, numSamples);
      juce::MidiBuffer midi;
      processor.processBlock(block, midi);
    }
  };
  auto outPeak = [&]() {
    float p = 0.0f;
    for (int i = 0; i < numSamples; ++i)
      p = juce::jmax(p, std::abs(block.getSample(0, i)));
    return juce::Decibels::gainToDecibels(p, -200.0f);
  };
  if (kneeParam != nullptr) {
    *kneeParam = 0.0f;
    runNearThresh(40);  // let the envelope settle from previous signals
    runNearThresh(1);
    std::cout << "[knee=0 ]     GR=" << processor.getGainReductionDb()
              << " OUT=" << outPeak() << std::endl;
    *kneeParam = 24.0f;
    runNearThresh(40);
    runNearThresh(1);
    std::cout << "[knee=24]     GR=" << processor.getGainReductionDb()
              << " OUT=" << outPeak() << std::endl;
    *kneeParam = 12.0f;
  }

  // ---- Sidechain HPF: bass should trigger less reduction when filtered ----
  auto *hpfParam =
      dynamic_cast<juce::AudioParameterFloat *>(processor.apvts.getParameter("hpf"));
  juce::AudioBuffer<float> bass(2, numSamples);
  for (int i = 0; i < numSamples; ++i) {
    const float v = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi *
                                    60.0f * (float)i / 44100.0f);  // -6 dBFS 60 Hz
    bass.setSample(0, i, v);
    bass.setSample(1, i, v);
  }
  auto runBass = [&](int count) {
    for (int r = 0; r < count; ++r) {
      block.copyFrom(0, 0, bass, 0, 0, numSamples);
      block.copyFrom(1, 0, bass, 1, 0, numSamples);
      juce::MidiBuffer midi;
      processor.processBlock(block, midi);
    }
  };
  if (hpfParam != nullptr) {
    *hpfParam = 0.0f;
    runBass(20);
    std::cout << "[hpf=off]     GR=" << processor.getGainReductionDb()
              << " OUT=" << processor.getOutputLevelDb() << std::endl;
    *hpfParam = 200.0f;
    runBass(20);
    std::cout << "[hpf=200Hz]  GR=" << processor.getGainReductionDb()
              << " OUT=" << processor.getOutputLevelDb() << std::endl;
    *hpfParam = 0.0f;
  }

  // ---- Log release shape: after a burst, GR must decay fast-then-slow ----
  juce::AudioBuffer<float> silence(2, numSamples);
  silence.clear();
  for (int r = 0; r < 6; ++r) {
    block.copyFrom(0, 0, sine, 0, 0, numSamples);
    block.copyFrom(1, 0, sine, 1, 0, numSamples);
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
  }
  std::cout << "[release]     burst GR=" << processor.getGainReductionDb()
            << std::endl;
  const int totalRelease = 60;
  float prevGr = processor.getGainReductionDb();
  for (int r = 0; r < totalRelease; ++r) {
    block.copyFrom(0, 0, silence, 0, 0, numSamples);
    block.copyFrom(1, 0, silence, 1, 0, numSamples);
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
    float gr = processor.getGainReductionDb();
    if (r < 6 || r % 10 == 0 || r == totalRelease - 1)
      std::cout << "[release]     t=" << (r + 1) * numSamples / 44100.0f
                << "s GR=" << gr << " dGR=" << (gr - prevGr) << std::endl;
    prevGr = gr;
  }

  // ---- AUTO release: fast for short transients, slow for sustained tone ----
  auto *releaseParam =
      dynamic_cast<juce::AudioParameterChoice *>(processor.apvts.getParameter("release"));
  if (releaseParam != nullptr) {
    auto setRelease = [&](int idx) { *releaseParam = idx; };
    auto runAudible = [&](int count) {
      for (int r = 0; r < count; ++r) {
        block.copyFrom(0, 0, sine, 0, 0, numSamples);
        block.copyFrom(1, 0, sine, 1, 0, numSamples);
        juce::MidiBuffer midi;
        processor.processBlock(block, midi);
      }
    };
    auto runSilent = [&](int count) {
      for (int r = 0; r < count; ++r) {
        block.copyFrom(0, 0, silence, 0, 0, numSamples);
        block.copyFrom(1, 0, silence, 1, 0, numSamples);
        juce::MidiBuffer midi;
        processor.processBlock(block, midi);
      }
    };

    setRelease((int)releaseParam->choices.size() - 1);  // AUTO

    // Short transient: a few blocks above threshold.
    runAudible(3);
    const float shortGr = processor.getGainReductionDb();
    runSilent(4);
    std::cout << "[auto-rel]    short burst GR=" << shortGr
              << " -> after 46ms silence=" << processor.getGainReductionDb()
              << std::endl;

    // Long sustained tone.
    runAudible(80);
    const float longGr = processor.getGainReductionDb();
    runSilent(4);
    std::cout << "[auto-rel]    long sustain GR=" << longGr
              << " -> after 46ms silence=" << processor.getGainReductionDb()
              << std::endl;

    setRelease(2);  // restore manual 100 ms
  }

  // ---- Lookahead master switch + host latency reporting ----
  auto *laOnParam = dynamic_cast<juce::AudioParameterBool *>(
      processor.apvts.getParameter("lookaheadOn"));
  auto *laParam = dynamic_cast<juce::AudioParameterFloat *>(
      processor.apvts.getParameter("lookahead"));
  auto *fbParam = dynamic_cast<juce::AudioParameterBool *>(
      processor.apvts.getParameter("feedback"));
  auto runOnce = [&]() {
    juce::MidiBuffer midi;
    processor.processBlock(block, midi);
  };
  if (laOnParam != nullptr && laParam != nullptr) {
    *laOnParam = false;
    *laParam = 5.0f;  // knob should be ignored while the switch is off
    runOnce();
    const int latOff = processor.getLatencySamples();
    *laOnParam = true;
    runOnce();
    const int latOn = processor.getLatencySamples();
    const int latExpected =
        juce::roundToInt(10.0 * 0.001 * 44100.0);  // rate passed to prepareToPlay
    std::cout << "[lookahead]   off lat=" << latOff << " (want 0), on lat="
              << latOn << " (want " << latExpected << ")" << std::endl;
    *laOnParam = false;
    *laParam = 0.0f;
  }

  // ---- Feedback topology: must compress a loud tone, then release on silence ----
  if (fbParam != nullptr) {
    *fbParam = true;
    for (int r = 0; r < 40; ++r) {
      block.copyFrom(0, 0, sine, 0, 0, numSamples);
      block.copyFrom(1, 0, sine, 1, 0, numSamples);
      runOnce();
    }
    std::cout << "[feedback]    tone  GR=" << processor.getGainReductionDb()
              << " OUT=" << processor.getOutputLevelDb() << std::endl;
    for (int r = 0; r < 12; ++r) {
      block.copyFrom(0, 0, silence, 0, 0, numSamples);
      block.copyFrom(1, 0, silence, 1, 0, numSamples);
      runOnce();
    }
    std::cout << "[feedback]    sil   GR=" << processor.getGainReductionDb()
              << std::endl;
    *fbParam = false;
    for (int r = 0; r < 5; ++r) runOnce();
  }

  auto snapshot = editor.createComponentSnapshot(editor.getLocalBounds());

  if (!snapshot.isValid()) {
    std::cerr << "Snapshot is invalid" << std::endl;
    return 2;
  }

  const int accentPixels =
      countCloseTo(snapshot, Theme::accent, 30);
  const int knobFacePixels =
      countCloseTo(snapshot, Theme::knobFace, 24);
  const int bezelPixels =
      countCloseTo(snapshot, Theme::knobBezel, 24);
  const int textPixels =
      countCloseTo(snapshot, Theme::text, 24);
  const int meterGreenPixels =
      countCloseTo(snapshot, juce::Colour(0xFF2EC4B6), 60);

  std::cout << "Image: " << snapshot.getWidth() << "x"
            << snapshot.getHeight() << std::endl;
  std::cout << "accent pixels:    " << accentPixels << std::endl;
  std::cout << "knobFace pixels:  " << knobFacePixels << std::endl;
  std::cout << "bezel pixels:     " << bezelPixels << std::endl;
  std::cout << "text pixels:      " << textPixels << std::endl;
  std::cout << "meterGreen pixels:" << meterGreenPixels << std::endl;

  // ---- The two new toggles must exist, be visible-sized and placed ----
  auto findSwitch = [&editor](const juce::String &text) -> juce::ToggleButton * {
    for (auto *child : editor.getChildren())
      if (auto *tb = dynamic_cast<juce::ToggleButton *>(child))
        if (tb->getButtonText() == text)
          return tb;
    return nullptr;
  };
  auto *laSwitch = findSwitch("LOOKAHEAD");
  auto *fbSwitch = findSwitch("FEEDBACK");
  const bool laOk =
      laSwitch != nullptr && laSwitch->getWidth() > 40 && laSwitch->isVisible();
  const bool fbOk =
      fbSwitch != nullptr && fbSwitch->getWidth() > 40 && fbSwitch->isVisible();
  std::cout << "[switches]    LOOKAHEAD ok=" << (laOk ? "yes" : "no")
            << " at (" << (laSwitch ? laSwitch->getX() : -1) << ","
            << (laSwitch ? laSwitch->getY() : -1) << ")" << std::endl;
  std::cout << "              FEEDBACK ok=" << (fbOk ? "yes" : "no")
            << " at (" << (fbSwitch ? fbSwitch->getX() : -1) << ","
            << (fbSwitch ? fbSwitch->getY() : -1) << ")" << std::endl;

  // ---- The LOOKAHEAD switch must sit below the LOOKAHEAD knob ----
  // SleekRotary is a Slider; the lookahead knob is the only one with a 0..10
  // range (the ratio knob is a 0..N-1 choice, hpf runs to 500 Hz).
  SleekRotary *laKnob = nullptr;
  for (auto *child : editor.getChildren())
    if (auto *sl = dynamic_cast<SleekRotary *>(child))
      if (std::abs(sl->getMaximum() - 10.0) < 0.01)
        laKnob = sl;
  const bool belowKnob =
      laKnob != nullptr && laSwitch != nullptr &&
      laSwitch->getY() > laKnob->getY() + laKnob->getHeight() - 10 &&
      std::abs(laSwitch->getBounds().getCentreX() -
               laKnob->getBounds().getCentreX()) < 60;
  std::cout << "[switches]    LOOKAHEAD below knob: "
            << (belowKnob ? "yes" : "no") << std::endl;

  bool ok = (accentPixels > 100 && knobFacePixels > 500 && bezelPixels > 200) &&
            laOk && fbOk && belowKnob;

  juce::File out =
      juce::File::getCurrentWorkingDirectory().getChildFile("render_test.png");
  juce::PNGImageFormat png;
  juce::FileOutputStream stream(out);
  png.writeImageToStream(snapshot, stream);

  std::cout << (ok ? "GUI OK - knobs rendered" : "GUI FAIL - knobs missing")
            << std::endl;
  return ok ? 0 : 1;
}