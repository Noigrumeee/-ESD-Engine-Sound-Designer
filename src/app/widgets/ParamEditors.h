#pragma once
// Editors generated from the parameter registry (model/Params.h): every row shows the
// modelling level badge (M = mechanical model, A = approximation, S = sound design only).
#include <JuceHeader.h>

#include "AppContext.h"
#include "model/Params.h"

namespace esd::ui
{
class ParamRow : public juce::Component
{
public:
    ParamRow (AppContext&, const ParamDef&);
    void refresh();
    bool shouldShow() const;
    void resized() override;
    void paint (juce::Graphics&) override;
    const ParamDef& def() const { return d; }

private:
    void push (double v);
    AppContext& ctx;
    const ParamDef& d;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    bool updating = false;
};

class ParamPanel : public juce::Component
{
public:
    ParamPanel (AppContext&, const juce::String& title, const std::vector<const ParamDef*>&);
    void refresh();
    int idealHeight() const;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    juce::OwnedArray<ParamRow> rows;
    static constexpr int kRowH = 28, kHeader = 26;
};

/// Vertical stack of panels inside a viewport (scrollable column of parameters).
class ParamColumn : public juce::Component
{
public:
    explicit ParamColumn (AppContext&);
    ParamPanel& addPanel (const juce::String& title, const std::vector<const ParamDef*>&);
    void addCustom (juce::Component& c, int height);
    void refresh();
    void resized() override;

private:
    struct Inner : juce::Component
    {
        std::vector<std::pair<juce::Component*, int>> items; // height <= 0: ask the ParamPanel
        void layout (int width);
    };
    AppContext& ctx;
    juce::Viewport viewport;
    Inner inner;
    juce::OwnedArray<ParamPanel> panels;
};
} // namespace esd::ui
