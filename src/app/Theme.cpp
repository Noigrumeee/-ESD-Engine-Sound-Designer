#include "Theme.h"

namespace esd::ui
{
juce::Colour tagColour (ParamTag t)
{
    switch (t)
    {
        case ParamTag::Mechanical: return colours::good;
        case ParamTag::Approximation: return colours::warn;
        case ParamTag::Acoustic: return colours::accent2;
    }
    return colours::textDim;
}

juce::String tagLetter (ParamTag t)
{
    switch (t)
    {
        case ParamTag::Mechanical: return "M";
        case ParamTag::Approximation: return "A";
        case ParamTag::Acoustic: return "S";
    }
    return "?";
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

Theme::Theme()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::backgroundColourId, background);
    setColour (juce::TextButton::buttonColourId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
    setColour (juce::Slider::backgroundColourId, outline);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, panel);
    setColour (juce::Slider::textBoxOutlineColourId, outline);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, panelRaised);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.35f));
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::outlineColourId, outline);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, accent.withAlpha (0.5f));
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ProgressBar::backgroundColourId, panelRaised);
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : bg;
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    if (down) c = c.brighter (0.15f);
    else if (highlighted) c = c.brighter (0.07f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::outline.brighter (highlighted ? 0.3f : 0.0f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const float cy = static_cast<float> (y) + static_cast<float> (h) * 0.5f;
    const auto track = juce::Rectangle<float> (static_cast<float> (x), cy - 2.0f, static_cast<float> (w), 4.0f);
    g.setColour (colours::outline);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (s.isEnabled() ? colours::accent : colours::textDim);
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    g.setColour (colours::text);
    g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
}

juce::Font Theme::getTextButtonFont (juce::TextButton&, int h) { return uiFont (juce::jmin (15.0f, static_cast<float> (h) * 0.55f)); }
juce::Font Theme::getLabelFont (juce::Label& l) { return l.getFont().withHeight (juce::jmax (12.0f, l.getFont().getHeight())); }
juce::Font Theme::getComboBoxFont (juce::ComboBox&) { return uiFont (14.0f); }
juce::Font Theme::getPopupMenuFont() { return uiFont (14.5f); }

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (12.5f, true));
        g.drawText (title.toUpperCase(), area.reduced (10, 6).removeFromTop (16), juce::Justification::centredLeft);
    }
}
} // namespace esd::ui
