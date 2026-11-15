#include "Page.h"

#include "model/Presets.h"

namespace esd::ui
{
namespace
{
class HomePage : public Page, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit HomePage (AppContext& c) : Page (c)
    {
        // ---- presets
        juce::String lastCat;
        for (auto& p : builtinPresets())
        {
            if (p.category != lastCat.toStdString())
            {
                lastCat = p.category;
                auto* l = presetHeaders.add (new juce::Label ({}, lastCat.toUpperCase()));
                l->setFont (uiFont (11.5f, true));
                l->setColour (juce::Label::textColourId, colours::textDim);
                presetInner.addAndMakeVisible (l);
                presetOrder.add (l);
            }
            auto* b = presetButtons.add (new juce::TextButton (p.name));
            b->setTooltip (p.description);
            const auto id = p.id;
            b->onClick = [this, id] { ctx.newFromPreset (id); };
            presetInner.addAndMakeVisible (b);
            presetOrder.add (b);
        }
        auto* note = presetHeaders.add (new juce::Label ({}, "Every preset is a complete, editable project (thermal, electric, hybrid, with its gearbox and vehicle)."));
        note->setFont (uiFont (12.0f));
        note->setColour (juce::Label::textColourId, colours::textDim);
        note->setJustificationType (juce::Justification::topLeft);
        presetInner.addAndMakeVisible (note);
        presetOrder.add (note);
        presetView.setViewedComponent (&presetInner, false);
        presetView.setScrollBarsShown (true, false);
        addAndMakeVisible (presetView);

        // ---- library
        search.setTextToShowWhenEmpty ("Search library...", colours::textDim);
        search.onTextChange = [this] { reload(); };
        addAndMakeVisible (search);
        category.onChange = [this] { reload(); };
        addAndMakeVisible (category);
        list.setModel (this);
        list.setRowHeight (40);
        addAndMakeVisible (list);
        for (auto* b : { &openBtn, &dupBtn, &revealBtn, &removeBtn, &trashBtn, &openFileBtn, &importBtn, &audioBtn })
            addAndMakeVisible (b);
        openBtn.onClick = [this] { if (auto* e = selected()) ctx.openFile (juce::File (e->path)); };
        dupBtn.onClick = [this] { if (auto* e = selected()) { ctx.duplicateFile (juce::File (e->path)); reload(); } };
        revealBtn.onClick = [this] { if (auto* e = selected()) juce::File (e->path).revealToUser(); };
        removeBtn.onClick = [this] { if (auto* e = selected()) { ctx.library.remove (e->id); reload(); } };
        trashBtn.onClick = [this] { trashSelected(); };
        openFileBtn.onClick = [this] { ctx.openDialog(); };
        importBtn.onClick = [this] { ctx.importDialog(); startTimer (1500); };
        audioBtn.onClick = [this] { showAudioSettings(); };
        revealBtn.setTooltip ("Show the project file in Explorer");
        removeBtn.setTooltip ("Remove from the library index (the file is kept)");
        trashBtn.setTooltip ("Move the project file to the Recycle Bin");

        // ---- recent + autosave
        recentLabel.setText ("RECENT", juce::dontSendNotification);
        autosaveLabel.setText ("UNSAVED SESSIONS (AUTOSAVE)", juce::dontSendNotification);
        for (auto* l : { &recentLabel, &autosaveLabel })
        {
            l->setFont (uiFont (11.5f, true));
            l->setColour (juce::Label::textColourId, colours::textDim);
            addAndMakeVisible (l);
        }
        info.setFont (uiFont (12.0f));
        info.setColour (juce::Label::textColourId, colours::textDim);
        info.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (info);
        reload();
    }

    void visibilityChanged() override
    {
        if (isShowing()) reload();
    }

    void projectChanged() override {}

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, presetPanel, "New project from preset");
        drawPanel (g, libraryPanel, "Engine library");
        drawPanel (g, sidePanel, {});
        g.setColour (colours::text);
        g.setFont (uiFont (28.0f, true));
        g.drawText ("Engine Sound Designer", titleArea, juce::Justification::left);
        g.setColour (colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Thermal, electric and hybrid powertrains, gearbox + vehicle, Sound Lab, loop sets, game exports.  "
                    "M / A / S badges: mechanical model, approximation, sound design.",
                    titleArea.withTrimmedLeft (330), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        titleArea = r.removeFromTop (44);
        r.removeFromTop (8);
        presetPanel = r.removeFromLeft (280);
        r.removeFromLeft (12);
        sidePanel = r.removeFromRight (300);
        r.removeFromRight (12);
        libraryPanel = r;

        auto p = presetPanel.reduced (10).withTrimmedTop (22);
        presetView.setBounds (p);
        int y = 0;
        const int w = p.getWidth() - 10;
        for (auto* c : presetOrder)
        {
            const bool isLabel = dynamic_cast<juce::Label*> (c) != nullptr;
            const int h = isLabel ? (c == presetOrder.getLast() ? 54 : 24) : 32;
            c->setBounds (0, y, w, h - (isLabel ? 0 : 4));
            y += h;
        }
        presetInner.setSize (w, y);

        auto l = libraryPanel.reduced (10).withTrimmedTop (22);
        auto top = l.removeFromTop (28);
        category.setBounds (top.removeFromRight (160));
        top.removeFromRight (8);
        search.setBounds (top);
        l.removeFromTop (8);
        auto buttons = l.removeFromBottom (30);
        const int bw = buttons.getWidth() / 5;
        for (auto* b : { &openBtn, &dupBtn, &revealBtn, &removeBtn, &trashBtn })
            b->setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
        l.removeFromBottom (8);
        list.setBounds (l);

        auto s = sidePanel.reduced (10);
        openFileBtn.setBounds (s.removeFromTop (30));
        s.removeFromTop (6);
        importBtn.setBounds (s.removeFromTop (30));
        s.removeFromTop (6);
        audioBtn.setBounds (s.removeFromTop (30));
        s.removeFromTop (12);
        recentLabel.setBounds (s.removeFromTop (20));
        for (auto* b : recentButtons) b->setBounds (s.removeFromTop (30).reduced (0, 2));
        s.removeFromTop (10);
        autosaveLabel.setBounds (s.removeFromTop (20));
        for (auto* b : autosaveButtons) b->setBounds (s.removeFromTop (30).reduced (0, 2));
        s.removeFromTop (10);
        info.setBounds (s);
    }

private:
    const LibraryEntry* selected() const
    {
        const int row = list.getSelectedRow();
        return row >= 0 && row < static_cast<int> (shown.size()) ? &shown[static_cast<std::size_t> (row)] : nullptr;
    }

    void timerCallback() override
    {
        stopTimer();
        reload();
    }

    void reload()
    {
        entries = ctx.library.all();
        // categories
        const auto current = category.getText();
        category.clear (juce::dontSendNotification);
        category.addItem ("All categories", 1);
        int id = 2;
        for (auto& c : ctx.library.categories()) category.addItem (c, id++);
        int sel = 1;
        for (int i = 0; i < category.getNumItems(); ++i)
            if (category.getItemText (i) == current) sel = category.getItemId (i);
        category.setSelectedId (sel, juce::dontSendNotification);

        shown.clear();
        const auto q = search.getText().trim().toLowerCase();
        const auto cat = category.getSelectedId() > 1 ? category.getText() : juce::String();
        for (auto& e : entries)
        {
            if (cat.isNotEmpty() && juce::String (e.category) != cat) continue;
            if (q.isNotEmpty() && ! juce::String (e.name + " " + e.category + " " + e.description).toLowerCase().contains (q)) continue;
            shown.push_back (e);
        }
        list.updateContent();
        list.repaint();

        recentButtons.clear();
        for (auto& e : ctx.library.recent (6))
        {
            auto* b = recentButtons.add (new juce::TextButton (e.name));
            b->setTooltip (e.path);
            const auto path = e.path;
            b->onClick = [this, path] { ctx.openFile (juce::File (path)); };
            addAndMakeVisible (b);
        }
        autosaveButtons.clear();
        for (auto& f : ctx.autosavedSessions())
        {
            auto* b = autosaveButtons.add (new juce::TextButton ("Recover " + f.getLastModificationTime().toString (true, true, false)));
            b->setTooltip (f.getFullPathName());
            b->onClick = [this, f] { ctx.openFile (f); };
            addAndMakeVisible (b);
        }
        juce::String s;
        s << "Projects folder:\n" << ctx.projectsDir.getFullPathName() << "\n\nLibrary index (SQLite):\n"
          << ctx.appDataDir.getChildFile ("library.db").getFullPathName();
        if (auto* dev = ctx.audio.deviceManager().getCurrentAudioDevice())
            s << "\n\nAudio: " << dev->getTypeName() << " - " << dev->getName() << "\n" << juce::String (juce::roundToInt (dev->getCurrentSampleRate())) << " Hz, "
              << dev->getCurrentBufferSizeSamples() << " samples (" << juce::String (dev->getCurrentBufferSizeSamples() * 1000.0 / dev->getCurrentSampleRate(), 1)
              << " ms)";
        else
            s << "\n\nAudio: no output device open";
        info.setText (s, juce::dontSendNotification);
        resized();
    }

    void trashSelected()
    {
        auto* e = selected();
        if (! e) return;
        const auto entry = *e;
        auto opts = juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle ("Move to Recycle Bin")
                        .withMessage ("Move \"" + juce::String (entry.name) + "\" (" + juce::String (entry.path) + ") to the Recycle Bin?")
                        .withButton ("Move to Recycle Bin")
                        .withButton ("Cancel");
        juce::AlertWindow::showAsync (opts, [this, entry] (int r)
        {
            if (r != 1) return;
            juce::File (entry.path).moveToTrash();
            ctx.library.remove (entry.id);
            reload();
        });
    }

    void showAudioSettings()
    {
        auto* sel = new juce::AudioDeviceSelectorComponent (ctx.audio.deviceManager(), 0, 0, 2, 2, false, false, true, false);
        sel->setSize (520, 380);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "Audio settings";
        o.dialogBackgroundColour = colours::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    // ListBoxModel
    int getNumRows() override { return static_cast<int> (shown.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selectedRow) override
    {
        if (row < 0 || row >= static_cast<int> (shown.size())) return;
        const auto& e = shown[static_cast<std::size_t> (row)];
        if (selectedRow)
        {
            g.setColour (colours::accent.withAlpha (0.2f));
            g.fillRect (0, 0, w, h);
        }
        const bool missing = ! juce::File (e.path).existsAsFile();
        g.setColour (missing ? colours::bad : colours::text);
        g.setFont (uiFont (14.5f, true));
        g.drawText (e.name + (missing ? "  (file missing)" : ""), 10, 3, w - 220, 20, juce::Justification::left);
        g.setColour (colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String (e.category) + "  -  " + juce::String (e.kind) + "  -  " + juce::String (e.description), 10, 21, w - 220, 16, juce::Justification::left);
        g.drawText (juce::Time (e.modified * 1000).toString (true, true, false), w - 200, 3, 190, 20, juce::Justification::right);
        g.setColour (colours::outline);
        g.drawHorizontalLine (h - 1, 0.0f, static_cast<float> (w));
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < static_cast<int> (shown.size())) ctx.openFile (juce::File (shown[static_cast<std::size_t> (row)].path));
    }

    juce::Viewport presetView;
    juce::Component presetInner;
    juce::OwnedArray<juce::TextButton> presetButtons;
    juce::OwnedArray<juce::Label> presetHeaders;
    juce::Array<juce::Component*> presetOrder;
    juce::TextEditor search;
    juce::ComboBox category;
    juce::ListBox list;
    juce::TextButton openBtn { "Open" }, dupBtn { "Duplicate" }, revealBtn { "Show file" }, removeBtn { "Remove" }, trashBtn { "Delete..." };
    juce::TextButton openFileBtn { "Open project file..." }, importBtn { "Import projects..." }, audioBtn { "Audio settings..." };
    juce::Label recentLabel, autosaveLabel, info;
    juce::OwnedArray<juce::TextButton> recentButtons, autosaveButtons;
    std::vector<LibraryEntry> entries, shown;
    juce::Rectangle<int> titleArea, presetPanel, libraryPanel, sidePanel;
};
} // namespace

std::unique_ptr<Page> createHomePage (AppContext& c) { return std::make_unique<HomePage> (c); }
} // namespace esd::ui
