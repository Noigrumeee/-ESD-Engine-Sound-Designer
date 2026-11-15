#pragma once
#include <JuceHeader.h>

#include "model/Params.h"

namespace esd::ui
{
namespace colours
{
inline const juce::Colour background { 0xff15171c };
inline const juce::Colour panel { 0xff1d2027 };
inline const juce::Colour panelRaised { 0xff252932 };
inline const juce::Colour outline { 0xff323743 };
inline const juce::Colour text { 0xffe3e6ec };
inline const juce::Colour textDim { 0xff8d94a3 };
inline const juce::Colour accent { 0xffff8a3d };      // orange
inline const juce::Colour accent2 { 0xff4cc2ff };     // cyan
inline const juce::Colour good { 0xff59d18b };
inline const juce::Colour warn { 0xffffc857 };
inline const juce::Colour bad { 0xffff5d5d };
inline const juce::Colour bankA { 0xffff8a3d };
inline const juce::Colour bankB { 0xff4cc2ff };
} // namespace colours

juce::Colour tagColour (ParamTag);
juce::String tagLetter (ParamTag);
juce::Font uiFont (float height, bool bold = false);

class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

/// Titled rounded panel drawn behind a group of controls.
void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
} // namespace esd::ui
