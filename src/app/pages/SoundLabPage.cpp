#include "Page.h"

#include "widgets/CurveEditor.h"
#include "widgets/ParamEditors.h"
#include "widgets/Visualizers.h"

namespace esd::ui
{
namespace
{
const char* kSourceNames[] = { "Rpm", "Torque", "Load", "Throttle", "Vehicle speed", "Gear", "Temperature", "Boost", "Electrical frequency", "Battery", "Regeneration", "Shift event" };
const char* kTargetNames[] = { "Gain (dB)", "Pan", "Low-pass (log 100 Hz .. 20 kHz)", "Saturation drive" };

/// Effect chain editor for one layer (built from the "fx.<Layer>" parameter group).
class FxEditor : public juce::Component
{
public:
    explicit FxEditor (AppContext& c) : ctx (c)
    {
        for (int i = 0; i < kNumLayers; ++i) layer.addItem (layerName (static_cast<LayerId> (i)), i + 1);
        layer.onChange = [this] { rebuild(); };
        irBtn.onClick = [this] { chooseIr(); };
        irClear.onClick = [this]
        {
            const auto li = static_cast<std::size_t> (layer.getSelectedId() - 1);
            ctx.doc.edit ([li] (Project& p) { p.mix.fx[li].irPath.clear(); }, "Remove impulse response");
        };
        irLabel.setFont (uiFont (12.0f));
        irLabel.setColour (juce::Label::textColourId, colours::textDim);
        for (auto* comp : std::initializer_list<juce::Component*> { &layer, &irBtn, &irClear, &irLabel })
            addAndMakeVisible (comp);
        layer.setSelectedId (static_cast<int> (LayerId::Exhaust) + 1, juce::dontSendNotification);
        rebuild();
    }
    void refresh()
    {
        if (panel) panel->refresh();
        const auto li = static_cast<std::size_t> (std::max (0, layer.getSelectedId() - 1));
        const auto& ir = ctx.doc.project().mix.fx[li].irPath;
        irLabel.setText (ir.empty() ? juce::String ("No impulse response (convolution off)") : juce::String (juce::CharPointer_UTF8 (ir.c_str())), juce::dontSendNotification);
    }
    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), "Layer effects"); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8).withTrimmedTop (20);
        layer.setBounds (r.removeFromTop (26));
        r.removeFromTop (4);
        auto ir = r.removeFromBottom (26);
        irBtn.setBounds (ir.removeFromLeft (140));
        irClear.setBounds (ir.removeFromLeft (70).reduced (4, 0));
        irLabel.setBounds (ir);
        if (panel) panel->setBounds (r);
    }

private:
    void rebuild()
    {
        const int i = layer.getSelectedId() - 1;
        if (i < 0) return;
        panel = std::make_unique<ParamPanel> (ctx, juce::String (layerName (static_cast<LayerId> (i))) + " chain",
                                              paramsInGroup (std::string ("fx.") + layerName (static_cast<LayerId> (i))));
        addAndMakeVisible (*panel);
        resized();
        refresh();
    }
    void chooseIr()
    {
        chooser = std::make_unique<juce::FileChooser> ("Impulse response (WAV / FLAC / AIFF)", juce::File(), "*.wav;*.flac;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            const auto li = static_cast<std::size_t> (layer.getSelectedId() - 1);
            const auto path = fc.getResult().getFullPathName().toStdString();
            ctx.doc.edit ([li, path] (Project& p)
            {
                p.mix.fx[li].irPath = path;
                if (p.mix.fx[li].convMix <= 0.0f) p.mix.fx[li].convMix = 0.5f;
            }, "Load impulse response");
        });
    }
    AppContext& ctx;
    juce::ComboBox layer;
    std::unique_ptr<ParamPanel> panel;
    juce::TextButton irBtn { "Impulse response..." }, irClear { "Clear" };
    juce::Label irLabel;
    std::unique_ptr<juce::FileChooser> chooser;
};

/// Automation list + editor: target(layer) += curve(source).
class AutomationEditor : public juce::Component, private juce::ListBoxModel
{
public:
    explicit AutomationEditor (AppContext& c) : ctx (c)
    {
        list.setModel (this);
        list.setRowHeight (22);
        for (int i = 0; i < kNumLayers; ++i) layer.addItem (layerName (static_cast<LayerId> (i)), i + 1);
        for (int i = 0; i < static_cast<int> (ModSource::Count); ++i) source.addItem (kSourceNames[i], i + 1);
        for (int i = 0; i < static_cast<int> (ModTarget::Count); ++i) target.addItem (kTargetNames[i], i + 1);
        enabled.setButtonText ("On");
        addBtn.onClick = [this] { add(); };
        removeBtn.onClick = [this] { remove(); };
        auto commit = [this] { if (! updating) apply ("Edit automation"); };
        layer.onChange = commit;
        source.onChange = commit;
        target.onChange = [this] { if (! updating) { apply ("Automation target"); show(); } };
        enabled.onClick = commit;
        curve.onEdit = [this] (const Curve&) { apply ("Edit automation curve", "autoCurve"); };
        curve.onEditEnd = [this] (const Curve&) { apply ("Edit automation curve", "autoCurve"); };
        for (auto* comp : std::initializer_list<juce::Component*> { &list, &addBtn, &removeBtn, &layer, &source, &target, &enabled, &curve })
            addAndMakeVisible (comp);
        refresh();
    }
    void refresh()
    {
        list.updateContent();
        list.repaint();
        show();
    }
    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), "Automation (parameter = f(variable), several can stack)"); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8).withTrimmedTop (20);
        auto top = r.removeFromTop (100);
        auto btns = top.removeFromRight (90);
        addBtn.setBounds (btns.removeFromTop (28));
        btns.removeFromTop (4);
        removeBtn.setBounds (btns.removeFromTop (28));
        list.setBounds (top.withTrimmedRight (6));
        r.removeFromTop (6);
        auto row = r.removeFromTop (26);
        enabled.setBounds (row.removeFromLeft (50));
        const int w = row.getWidth() / 3;
        layer.setBounds (row.removeFromLeft (w).reduced (2, 0));
        source.setBounds (row.removeFromLeft (w).reduced (2, 0));
        target.setBounds (row.reduced (2, 0));
        r.removeFromTop (4);
        curve.setBounds (r);
    }

private:
    int sel() const
    {
        const int i = list.getSelectedRow();
        return i >= 0 && i < static_cast<int> (ctx.doc.project().mix.automations.size()) ? i : -1;
    }
    void show()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const int i = sel();
        for (auto* comp : std::initializer_list<juce::Component*> { &layer, &source, &target, &enabled, &curve, &removeBtn })
            comp->setEnabled (i >= 0);
        if (i < 0) { curve.setCurve (Curve()); return; }
        const auto& a = ctx.doc.project().mix.automations[static_cast<std::size_t> (i)];
        layer.setSelectedId (a.layer + 1, juce::dontSendNotification);
        source.setSelectedId (static_cast<int> (a.source) + 1, juce::dontSendNotification);
        target.setSelectedId (static_cast<int> (a.target) + 1, juce::dontSendNotification);
        enabled.setToggleState (a.enabled, juce::dontSendNotification);
        curve.setCurve (a.curve);
        switch (a.target)
        {
            case ModTarget::GainDb: curve.setRanges (0, 1, -48, 12); curve.setAxisLabels (kSourceNames[static_cast<int> (a.source)], "gain offset (dB)"); break;
            case ModTarget::Pan: curve.setRanges (0, 1, -1, 1); curve.setAxisLabels (kSourceNames[static_cast<int> (a.source)], "pan offset"); break;
            case ModTarget::LowpassHz: curve.setRanges (0, 1, 0, 1); curve.setAxisLabels (kSourceNames[static_cast<int> (a.source)], "low-pass cutoff (0 = 100 Hz, 1 = 20 kHz)"); break;
            default: curve.setRanges (0, 1, 0, 1); curve.setAxisLabels (kSourceNames[static_cast<int> (a.source)], "saturation drive"); break;
        }
    }
    void apply (const juce::String& what, const juce::String& key = {})
    {
        const int i = sel();
        if (i < 0) return;
        Automation a = ctx.doc.project().mix.automations[static_cast<std::size_t> (i)];
        a.layer = layer.getSelectedId() - 1;
        a.source = static_cast<ModSource> (source.getSelectedId() - 1);
        const auto newTarget = static_cast<ModTarget> (target.getSelectedId() - 1);
        if (newTarget != a.target)
            a.curve = newTarget == ModTarget::LowpassHz ? Curve ({ { 0, 1 }, { 1, 1 } }) : Curve ({ { 0, 0 }, { 1, 0 } });
        else
            a.curve = curve.curve();
        a.target = newTarget;
        a.enabled = enabled.getToggleState();
        ctx.doc.edit ([i, a] (Project& p) { p.mix.automations[static_cast<std::size_t> (i)] = a; }, what, key);
    }
    void add()
    {
        Automation a;
        a.layer = static_cast<int> (LayerId::Intake);
        a.source = ModSource::Load;
        a.target = ModTarget::GainDb;
        a.curve = Curve ({ { 0.0, -12.0 }, { 0.5, -4.0 }, { 1.0, 3.0 } });
        ctx.doc.edit ([a] (Project& p) { p.mix.automations.push_back (a); }, "Add automation");
        list.updateContent();
        list.selectRow (static_cast<int> (ctx.doc.project().mix.automations.size()) - 1);
    }
    void remove()
    {
        const int i = sel();
        if (i < 0) return;
        ctx.doc.edit ([i] (Project& p) { p.mix.automations.erase (p.mix.automations.begin() + i); }, "Remove automation");
        refresh();
    }
    int getNumRows() override { return static_cast<int> (ctx.doc.project().mix.automations.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool s) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& a = ctx.doc.project().mix.automations[static_cast<std::size_t> (row)];
        if (s) { g.setColour (colours::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
        g.setColour (a.enabled ? colours::text : colours::textDim);
        g.setFont (uiFont (12.5f));
        g.drawText (juce::String (layerName (static_cast<LayerId> (a.layer))) + "  " + kTargetNames[static_cast<int> (a.target)] + "  <-  "
                        + kSourceNames[static_cast<int> (a.source)],
                    6, 0, w - 12, h, juce::Justification::centredLeft);
    }
    void selectedRowsChanged (int) override { show(); }

    AppContext& ctx;
    juce::ListBox list;
    juce::TextButton addBtn { "Add" }, removeBtn { "Remove" };
    juce::ComboBox layer, source, target;
    juce::ToggleButton enabled;
    CurveEditor curve;
    bool updating = false;
};

class SoundLabPage : public Page
{
public:
    explicit SoundLabPage (AppContext& c)
        : Page (c), mixer (c), fx (c), automation (c), analyzer (c, AnalyzerPanel::Spectrum | AnalyzerPanel::Spectrogram), live (c),
          output (c, "Output & shared reverb", { findParam ("mix.width"), findParam ("mix.master"), findParam ("mix.reverbSize"), findParam ("mix.reverbDamp"),
                                                 findParam ("mix.reverbReturn") })
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &mixer, &fx, &automation, &analyzer, &live, &output })
            addAndMakeVisible (comp);
    }

    void projectChanged() override
    {
        mixer.refresh();
        fx.refresh();
        automation.refresh();
        output.refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        live.setBounds (r.removeFromBottom (48));
        r.removeFromBottom (8);
        auto left = r.removeFromLeft (juce::jmin (540, r.getWidth() * 36 / 100));
        mixer.setBounds (left.removeFromTop (left.getHeight() * 55 / 100));
        left.removeFromTop (8);
        output.setBounds (left.removeFromTop (output.idealHeight()));
        r.removeFromLeft (10);
        auto mid = r.removeFromLeft (r.getWidth() / 2);
        fx.setBounds (mid.removeFromTop (juce::jmin (mid.getHeight() * 62 / 100, 420)));
        mid.removeFromTop (8);
        automation.setBounds (mid);
        r.removeFromLeft (10);
        analyzer.setBounds (r);
    }

private:
    LayerMixer mixer;
    FxEditor fx;
    AutomationEditor automation;
    AnalyzerPanel analyzer;
    LiveControls live;
    ParamPanel output;
};
} // namespace

std::unique_ptr<Page> createSoundLabPage (AppContext& c) { return std::make_unique<SoundLabPage> (c); }
} // namespace esd::ui
