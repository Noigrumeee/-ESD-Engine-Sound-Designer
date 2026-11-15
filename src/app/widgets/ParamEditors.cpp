#include "ParamEditors.h"
#include "Theme.h"

namespace esd::ui
{
ParamRow::ParamRow (AppContext& c, const ParamDef& def) : ctx (c), d (def)
{
    label.setText (d.label, juce::dontSendNotification);
    label.setFont (uiFont (13.5f));
    label.setColour (juce::Label::textColourId, colours::text);
    const juce::String tip = juce::String (d.help) + "\n[" + toString (d.tag) + "]";
    label.setTooltip (tip);
    addAndMakeVisible (label);

    switch (d.kind)
    {
        case ParamDef::Kind::Number:
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 20);
            const double step = d.decimals > 0 ? std::pow (10.0, -d.decimals) : 1.0;
            slider.setRange (d.min, d.max, step);
            slider.setNumDecimalPlacesToDisplay (d.decimals);
            if (d.max / std::max (1e-9, d.min) > 50.0 && d.min > 0.0)
                slider.setSkewFactorFromMidPoint (std::sqrt (d.min * d.max));
            slider.setTextValueSuffix (d.unit.empty() ? juce::String() : " " + juce::String (d.unit));
            slider.setTooltip (tip);
            slider.setDoubleClickReturnValue (false, 0.0);
            slider.onValueChange = [this] { if (! updating) push (slider.getValue()); };
            addAndMakeVisible (slider);
            break;
        }
        case ParamDef::Kind::Choice:
        {
            int id = 1;
            for (auto& ch : d.choices) combo.addItem (ch, id++);
            combo.setTooltip (tip);
            combo.onChange = [this] { if (! updating) push (combo.getSelectedId() - 1); };
            addAndMakeVisible (combo);
            break;
        }
        case ParamDef::Kind::Toggle:
            toggle.setTooltip (tip);
            toggle.onClick = [this] { if (! updating) push (toggle.getToggleState() ? 1.0 : 0.0); };
            addAndMakeVisible (toggle);
            break;
    }
    refresh();
}

void ParamRow::push (double v)
{
    const bool mixOnly = d.group == "mix";
    const auto* def = &d;
    ctx.doc.edit ([def, v] (Project& p) { def->set (p, v); }, "Change " + juce::String (d.label), d.id, mixOnly);
}

bool ParamRow::shouldShow() const { return ! d.visible || d.visible (ctx.doc.project()); }

void ParamRow::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const double v = d.get (ctx.doc.project());
    switch (d.kind)
    {
        case ParamDef::Kind::Number: slider.setValue (v, juce::dontSendNotification); break;
        case ParamDef::Kind::Choice: combo.setSelectedId (static_cast<int> (std::lround (v)) + 1, juce::dontSendNotification); break;
        case ParamDef::Kind::Toggle: toggle.setToggleState (v >= 0.5, juce::dontSendNotification); break;
    }
}

void ParamRow::paint (juce::Graphics& g)
{
    auto badge = getLocalBounds().removeFromLeft (18).withSizeKeepingCentre (16, 16).toFloat();
    g.setColour (tagColour (d.tag).withAlpha (0.2f));
    g.fillRoundedRectangle (badge, 3.0f);
    g.setColour (tagColour (d.tag));
    g.setFont (uiFont (11.0f, true));
    g.drawText (tagLetter (d.tag), badge, juce::Justification::centred);
}

void ParamRow::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (22);
    label.setBounds (r.removeFromLeft (juce::jmax (110, r.getWidth() * 38 / 100)));
    auto c = r.reduced (0, 2);
    slider.setBounds (c);
    combo.setBounds (c);
    toggle.setBounds (c);
}

ParamPanel::ParamPanel (AppContext& ctx, const juce::String& t, const std::vector<const ParamDef*>& defs) : title (t)
{
    for (auto* d : defs)
        addAndMakeVisible (rows.add (new ParamRow (ctx, *d)));
    refresh();
}

void ParamPanel::refresh()
{
    bool layoutChanged = false;
    for (auto* r : rows)
    {
        r->refresh();
        const bool show = r->shouldShow();
        if (show != r->isVisible())
        {
            r->setVisible (show);
            layoutChanged = true;
        }
    }
    if (layoutChanged)
    {
        resized();
        if (auto* p = getParentComponent()) p->resized();
    }
}

int ParamPanel::idealHeight() const
{
    int n = 0;
    for (auto* r : rows)
        if (r->shouldShow()) ++n;
    return kHeader + n * kRowH + 8;
}

void ParamPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (kHeader);
    for (auto* row : rows)
        if (row->isVisible()) row->setBounds (r.removeFromTop (kRowH));
}

void ParamPanel::paint (juce::Graphics& g) { drawPanel (g, getLocalBounds(), title); }

ParamColumn::ParamColumn (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&inner, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

ParamPanel& ParamColumn::addPanel (const juce::String& title, const std::vector<const ParamDef*>& defs)
{
    auto* p = panels.add (new ParamPanel (ctx, title, defs));
    inner.addAndMakeVisible (p);
    inner.items.emplace_back (p, 0);
    return *p;
}

void ParamColumn::addCustom (juce::Component& c, int height)
{
    inner.addAndMakeVisible (c);
    inner.items.emplace_back (&c, height);
}

void ParamColumn::refresh()
{
    for (auto* p : panels) p->refresh();
    inner.layout (viewport.getMaximumVisibleWidth());
}

void ParamColumn::resized()
{
    viewport.setBounds (getLocalBounds());
    inner.layout (viewport.getMaximumVisibleWidth());
}

void ParamColumn::Inner::layout (int width)
{
    int y = 0;
    for (auto& [c, h] : items)
    {
        int height = h;
        if (height <= 0)
            if (auto* p = dynamic_cast<ParamPanel*> (c)) height = p->idealHeight();
        c->setBounds (0, y, width, height);
        y += height + 8;
    }
    setSize (width, y);
}
} // namespace esd::ui
