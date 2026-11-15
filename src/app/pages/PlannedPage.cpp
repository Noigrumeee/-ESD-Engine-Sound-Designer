#include "Page.h"

namespace esd::ui
{
// ===================================================================== LiveControls
LiveControls::LiveControls (AppContext& c) : ctx (c)
{
    startStop.onClick = [this]
    {
        if (ctx.audio.ignition.load()) ctx.audio.stopEngine();
        else ctx.audio.startEngineInstant();
    };
    auto setupSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double step)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        s.setRange (lo, hi, step);
        l.setText (name, juce::dontSendNotification);
        l.setFont (uiFont (12.5f));
        l.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (s);
        addAndMakeVisible (l);
    };
    setupSlider (throttle, throttleLabel, "Throttle", 0.0, 1.0, 0.01);
    setupSlider (rpm, rpmLabel, "rpm", 500.0, 12000.0, 10.0);
    setupSlider (load, loadLabel, "Load", 0.0, 1.0, 0.01);
    throttle.onValueChange = [this] { ctx.audio.throttle.store (static_cast<float> (throttle.getValue())); };
    rpm.onValueChange = [this] { ctx.audio.dynoRpm.store (static_cast<float> (rpm.getValue())); };
    load.onValueChange = [this] { ctx.audio.dynoLoad.store (static_cast<float> (load.getValue())); };
    dyno.onClick = [this]
    {
        ctx.audio.dynoMode.store (dyno.getToggleState());
        if (dyno.getToggleState() && ! ctx.audio.ignition.load()) ctx.audio.startEngineInstant();
    };
    readout.setFont (uiFont (15.0f, true));
    readout.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (startStop);
    addAndMakeVisible (dyno);
    addAndMakeVisible (readout);
    startTimerHz (15);
}

void LiveControls::visibilityChanged() {}

void LiveControls::timerCallback()
{
    if (! isShowing()) return;
    const bool on = ctx.audio.ignition.load();
    startStop.setButtonText (on ? "Stop engine" : "Start engine");
    startStop.setToggleState (on, juce::dontSendNotification);
    throttle.setValue (ctx.audio.throttle.load(), juce::dontSendNotification);
    dyno.setToggleState (ctx.audio.dynoMode.load(), juce::dontSendNotification);
    rpm.setValue (ctx.audio.dynoRpm.load(), juce::dontSendNotification);
    load.setValue (ctx.audio.dynoLoad.load(), juce::dontSendNotification);
    const bool d = dyno.getToggleState();
    rpm.setEnabled (d);
    load.setEnabled (d);
    throttle.setEnabled (! d);
    const auto t = ctx.audio.telemetry();
    readout.setText (juce::String (juce::roundToInt (t.rpm)) + " rpm  " + juce::String (juce::roundToInt (t.airflow * 100)) + " % load", juce::dontSendNotification);
    rpm.setRange (500.0, juce::jmax (1000.0, ctx.doc.project().engine.limiterRpm), 10.0);
}

void LiveControls::paint (juce::Graphics& g) { drawPanel (g, getLocalBounds(), {}); }

void LiveControls::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    startStop.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (10);
    readout.setBounds (r.removeFromRight (190));
    const int w = r.getWidth();
    auto part = [&r] (int width) { return r.removeFromLeft (width); };
    auto t = part (w * 30 / 100);
    throttleLabel.setBounds (t.removeFromLeft (58));
    throttle.setBounds (t);
    dyno.setBounds (part (w * 18 / 100).reduced (6, 0));
    auto a = part (w * 28 / 100);
    rpmLabel.setBounds (a.removeFromLeft (30));
    rpm.setBounds (a);
    auto b = r;
    loadLabel.setBounds (b.removeFromLeft (40));
    load.setBounds (b);
}

// ===================================================================== PlannedPage
namespace
{
class PlannedPage : public Page
{
public:
    PlannedPage (AppContext& c, juce::String t, juce::String ph, juce::StringArray b) : Page (c), title (std::move (t)), phase (std::move (ph)), bullets (std::move (b)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (24);
        drawPanel (g, r, {});
        r = r.reduced (28);
        g.setColour (colours::text);
        g.setFont (uiFont (26.0f, true));
        g.drawText (title, r.removeFromTop (40), juce::Justification::left);
        g.setColour (colours::warn);
        g.setFont (uiFont (15.0f, true));
        g.drawText ("Not implemented yet - planned for " + phase + ".", r.removeFromTop (28), juce::Justification::left);
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText ("This workspace has no controls on purpose: nothing here is simulated or faked. "
                          "The architecture already reserves its place (PowertrainKind, layer system, runtime swap). Planned content:",
                          r.removeFromTop (48), juce::Justification::topLeft, 3);
        r.removeFromTop (8);
        g.setFont (uiFont (14.5f));
        for (auto& b : bullets)
        {
            g.setColour (colours::accent);
            g.fillEllipse (static_cast<float> (r.getX()), static_cast<float> (r.getY()) + 8.0f, 6.0f, 6.0f);
            g.setColour (colours::text);
            g.drawFittedText (b, r.removeFromTop (24).withTrimmedLeft (16), juce::Justification::left, 1);
        }
    }

private:
    juce::String title, phase;
    juce::StringArray bullets;
};
} // namespace

std::unique_ptr<Page> createPlannedPage (AppContext& c, const juce::String& title, const juce::String& phase, const juce::StringArray& bullets)
{
    return std::make_unique<PlannedPage> (c, title, phase, bullets);
}
} // namespace esd::ui
