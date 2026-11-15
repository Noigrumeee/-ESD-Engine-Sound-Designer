#pragma once
#include <JuceHeader.h>

#include "AppContext.h"
#include "Theme.h"

namespace esd::ui
{
class Page : public juce::Component
{
public:
    explicit Page (AppContext& c) : ctx (c) {}
    /// Called on the message thread after any project change (edit, undo, load).
    virtual void projectChanged() {}

protected:
    AppContext& ctx;
};

/// Compact live engine controls shown on design pages so every change can be heard at once.
class LiveControls : public juce::Component, private juce::Timer
{
public:
    explicit LiveControls (AppContext&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    AppContext& ctx;
    juce::TextButton startStop { "Start engine" };
    juce::ToggleButton dyno { "Hold rpm (dyno)" };
    juce::Slider throttle, rpm, load;
    juce::Label throttleLabel, rpmLabel, loadLabel, readout;
};

std::unique_ptr<Page> createHomePage (AppContext&);
std::unique_ptr<Page> createEngineBuilderPage (AppContext&);
std::unique_ptr<Page> createExhaustPage (AppContext&);
std::unique_ptr<Page> createSimulatorPage (AppContext&);
std::unique_ptr<Page> createSoundLabPage (AppContext&);
std::unique_ptr<Page> createSampleEditorPage (AppContext&);
std::unique_ptr<Page> createExportPage (AppContext&);
std::unique_ptr<Page> createElectricPage (AppContext&);
std::unique_ptr<Page> createHybridPage (AppContext&);
std::unique_ptr<Page> createDrivetrainPage (AppContext&);
std::unique_ptr<Page> createLoopGeneratorPage (AppContext&);
std::unique_ptr<juce::Component> createGameExportPanel (AppContext&);
std::unique_ptr<Page> createPlannedPage (AppContext&, const juce::String& title, const juce::String& phase, const juce::StringArray& bullets);
} // namespace esd::ui
