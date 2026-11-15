#pragma once
#include <JuceHeader.h>

#include "AppContext.h"
#include "model/EngineLayout.h"

namespace esd::ui
{
/// Polar + timeline view of the firing events (computed from the crank geometry).
class FiringDiagram : public juce::Component
{
public:
    void setPlan (const FiringPlan& p, int banks);
    void setCrankAngle (float deg) { crank = deg; repaint(); }
    void paint (juce::Graphics&) override;

private:
    FiringPlan plan;
    int banks = 1;
    float crank = -1.0f;
};

/// Simplified schematic of the exhaust system with the derived acoustic numbers.
class ExhaustDiagram : public juce::Component
{
public:
    void setProject (const Project& p) { proj = p; repaint(); }
    void paint (juce::Graphics&) override;

private:
    Project proj;
};

/// Live FFT spectrum + spectrogram + scope fed by the audio host analysis FIFO.
class AnalyzerPanel : public juce::Component, private juce::Timer
{
public:
    enum Flags { Spectrum = 1, Spectrogram = 2, Scope = 4 };
    AnalyzerPanel (AppContext&, int flags);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    /// Dominant peaks of the last analysis (Hz, dB).
    const std::vector<std::pair<float, float>>& peaks() const { return dominant; }

private:
    void timerCallback() override;
    void paintSpectrum (juce::Graphics&, juce::Rectangle<int>);
    void paintScope (juce::Graphics&, juce::Rectangle<int>);
    float xForFreq (float f, juce::Rectangle<float> a) const;

    AppContext& ctx;
    int flags;
    static constexpr int kOrder = 13, kSize = 1 << kOrder;
    juce::dsp::FFT fft { kOrder };
    juce::dsp::WindowingFunction<float> window { kSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> ring = std::vector<float> (kSize, 0.0f);
    std::vector<float> fftData = std::vector<float> (2 * kSize, 0.0f);
    std::vector<float> specDb = std::vector<float> (kSize / 2, -120.0f);
    std::vector<float> scratch = std::vector<float> (kSize, 0.0f);
    std::vector<std::pair<float, float>> dominant;
    juce::Image sonogram;
    juce::Rectangle<int> specArea, sonoArea, scopeArea;
    float rpm = 0.0f, firingHz = 0.0f;
};

/// Per-layer mixer rows (mute / solo / gain / pan / level meter). Edits the project mix.
class LayerMixer : public juce::Component, private juce::Timer
{
public:
    explicit LayerMixer (AppContext&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    struct Row
    {
        juce::Label name;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Slider gain, pan;
        float level = 0.0f;
    };
    AppContext& ctx;
    std::array<std::unique_ptr<Row>, kNumLayers> rows;
    juce::Slider master;
    juce::Label masterLabel;
    bool updating = false;
};

/// Small numeric telemetry tile with optional bar.
class Readout : public juce::Component
{
public:
    Readout (juce::String title, juce::String unit, float maxValue = 0.0f) : t (std::move (title)), u (std::move (unit)), maxV (maxValue) {}
    void set (float v, int decimals = 0, juce::Colour c = juce::Colour());
    void setText (const juce::String& s, juce::Colour c = juce::Colour());
    void setTitle (const juce::String& title, const juce::String& unit)
    {
        if (title != t || unit != u) { t = title; u = unit; repaint(); }
    }
    void paint (juce::Graphics&) override;

private:
    juce::String t, u, text;
    float value = 0, maxV;
    juce::Colour colour;
};
} // namespace esd::ui
