// Engine Sound Designer - application entry point, main window and navigation.
#include <JuceHeader.h>

#include "AppContext.h"
#include "Theme.h"
#include "pages/Page.h"

namespace esd::ui
{
class MainComponent : public juce::Component, private juce::ChangeListener, private juce::Timer, private juce::KeyListener
{
public:
    MainComponent()
    {
        setLookAndFeel (&theme);
        juce::LookAndFeel::setDefaultLookAndFeel (&theme);

        ctx.status = [this] (const juce::String& m, bool err) { showStatus (m, err); };
        ctx.navigate = [this] (const juce::String& id) { showPage (id); };

        struct Def { const char* id; const char* label; std::function<std::unique_ptr<Page>()> make; };
        const std::vector<Def> defs = {
            { "home", "Home", [this] { return createHomePage (ctx); } },
            { "builder", "Engine Builder", [this] { return createEngineBuilderPage (ctx); } },
            { "electric", "Electric Motor", [this] { return createElectricPage (ctx); } },
            { "hybrid", "Hybrid Powertrain", [this] { return createHybridPage (ctx); } },
            { "exhaust", "Exhaust Designer", [this] { return createExhaustPage (ctx); } },
            { "soundlab", "Sound Lab", [this] { return createSoundLabPage (ctx); } },
            { "simulator", "Engine Simulator", [this] { return createSimulatorPage (ctx); } },
            { "samples", "Sample Editor", [this] { return createSampleEditorPage (ctx); } },
            { "drivetrain", "Transmission & Vehicle", [this] { return createDrivetrainPage (ctx); } },
            { "loops", "Loop Generator", [this] { return createLoopGeneratorPage (ctx); } },
            { "export", "Export Studio", [this] { return createExportPage (ctx); } },
        };
        for (auto& d : defs)
        {
            auto* b = navButtons.add (new juce::TextButton (d.label));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1);
            const juce::String id = d.id;
            b->onClick = [this, id] { showPage (id); };
            addAndMakeVisible (b);
            auto page = d.make();
            addChildComponent (*page);
            pages.add (page.release());
            pageIds.add (d.id);
        }

        projectTitle.setFont (uiFont (16.0f, true));
        addAndMakeVisible (projectTitle);
        for (auto* b : { &undoBtn, &redoBtn, &saveBtn, &saveAsBtn, &engineBtn })
            addAndMakeVisible (b);
        undoBtn.onClick = [this] { ctx.doc.undo(); };
        redoBtn.onClick = [this] { ctx.doc.redo(); };
        saveBtn.onClick = [this] { ctx.save(); };
        saveAsBtn.onClick = [this] { ctx.saveAsDialog(); };
        engineBtn.onClick = [this] { toggleEngine(); };
        undoBtn.setTooltip ("Undo (Ctrl+Z)");
        redoBtn.setTooltip ("Redo (Ctrl+Y)");
        saveBtn.setTooltip ("Save (Ctrl+S). A new project is saved into the library folder.");
        engineBtn.setTooltip ("Start / stop the engine (Space). Hold Up arrow for full throttle, 0-9 for 0-90 %");
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.5, 0.01);
        volume.setValue (0.8);
        volume.setTooltip ("Monitor volume (does not change the project or the renders)");
        volume.onValueChange = [this] { ctx.audio.monitorGain.store (static_cast<float> (volume.getValue())); };
        ctx.audio.monitorGain.store (0.8f);
        addAndMakeVisible (volume);
        cpu.setFont (uiFont (12.0f));
        cpu.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (cpu);
        status.setFont (uiFont (12.5f));
        addAndMakeVisible (status);

        ctx.doc.addChangeListener (this);
        addKeyListener (this);
        setWantsKeyboardFocus (true);
        updateTitle();
        showPage ("home");
        startTimerHz (30);
        setSize (1500, 920);
    }

    ~MainComponent() override
    {
        ctx.doc.removeChangeListener (this);
        removeKeyListener (this);
        pages.clear();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    AppContext& context() { return ctx; }

    /// Developer self-check: renders every page to PNG files (used to verify layouts), then quits.
    void runSnapshots (const juce::File& dir)
    {
        dir.createDirectory();
        volume.setValue (0.0); // silent: the analyzers read the project output before the monitor volume
        ctx.audio.dynoMode.store (true);
        ctx.audio.dynoRpm.store (3200.0f);
        ctx.audio.dynoLoad.store (0.7f);
        ctx.audio.startEngineInstant();
        auto step = std::make_shared<std::function<void (int)>>();
        *step = [this, dir, step] (int i)
        {
            if (i > 0)
            {
                auto img = createComponentSnapshot (getLocalBounds());
                juce::FileOutputStream out (dir.getChildFile (juce::String (i - 1).paddedLeft ('0', 2) + "_" + pageIds[i - 1] + ".png"));
                if (out.openedOk())
                {
                    out.setPosition (0);
                    out.truncate();
                    juce::PNGImageFormat().writeImageToStream (img, out);
                }
            }
            if (i >= pages.size())
            {
                juce::JUCEApplication::getInstance()->quit();
                return;
            }
            showPage (pageIds[i]);
            juce::Timer::callAfterDelay (1200, [step, i] { (*step) (i + 1); });
        };
        juce::Timer::callAfterDelay (1500, [step] { (*step) (0); });
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        g.setColour (colours::panel);
        g.fillRect (getLocalBounds().removeFromTop (kTop));
        g.fillRect (getLocalBounds().withTrimmedTop (kTop).removeFromLeft (kSide));
        g.fillRect (getLocalBounds().removeFromBottom (kStatus));
        g.setColour (colours::outline);
        g.drawHorizontalLine (kTop - 1, 0.0f, static_cast<float> (getWidth()));
        g.drawVerticalLine (kSide - 1, static_cast<float> (kTop), static_cast<float> (getHeight() - kStatus));
        g.drawHorizontalLine (getHeight() - kStatus, 0.0f, static_cast<float> (getWidth()));
        g.setColour (colours::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText ("v" + juce::String (ProjectInfo::versionString) + "  -  M mechanical  A approx.  S sound design",
                    juce::Rectangle<int> (8, getHeight() - kStatus - 24, kSide - 16, 20), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (kTop).reduced (10, 8);
        engineBtn.setBounds (top.removeFromRight (130));
        top.removeFromRight (10);
        cpu.setBounds (top.removeFromRight (90));
        volume.setBounds (top.removeFromRight (120));
        top.removeFromRight (10);
        saveAsBtn.setBounds (top.removeFromRight (90));
        top.removeFromRight (4);
        saveBtn.setBounds (top.removeFromRight (70));
        top.removeFromRight (12);
        redoBtn.setBounds (top.removeFromRight (60));
        top.removeFromRight (4);
        undoBtn.setBounds (top.removeFromRight (60));
        projectTitle.setBounds (top);
        status.setBounds (r.removeFromBottom (kStatus).reduced (10, 2));
        auto side = r.removeFromLeft (kSide).reduced (8, 10);
        for (auto* b : navButtons)
        {
            b->setBounds (side.removeFromTop (36));
            side.removeFromTop (4);
        }
        for (auto* p : pages) p->setBounds (r);
    }

    void showPage (const juce::String& id)
    {
        const int idx = pageIds.indexOf (id);
        if (idx < 0) return;
        for (int i = 0; i < pages.size(); ++i)
            pages[i]->setVisible (i == idx);
        navButtons[idx]->setToggleState (true, juce::dontSendNotification);
        pages[idx]->projectChanged();
    }

private:
    static constexpr int kTop = 48, kSide = 190, kStatus = 26;

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateTitle();
        for (auto* p : pages)
            if (p->isVisible()) p->projectChanged();
        // hidden pages refresh when shown (showPage)
    }

    void updateTitle()
    {
        const auto& d = ctx.doc;
        juce::String t = juce::String (d.project().name) + (d.isDirty() ? "  *" : "");
        t << "    " << (d.file() == juce::File() ? juce::String ("(not saved yet)") : d.file().getFullPathName());
        projectTitle.setText (t, juce::dontSendNotification);
        undoBtn.setEnabled (d.canUndo());
        redoBtn.setEnabled (d.canRedo());
        undoBtn.setTooltip (d.canUndo() ? "Undo: " + d.undoDescription() + " (Ctrl+Z)" : juce::String ("Nothing to undo"));
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName ("Engine Sound Designer - " + juce::String (d.project().name) + (d.isDirty() ? " *" : ""));
    }

    void showStatus (const juce::String& m, bool err)
    {
        status.setText (m, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, err ? colours::bad : colours::text);
        statusAge = 0;
    }

    void toggleEngine()
    {
        if (ctx.audio.ignition.load()) ctx.audio.stopEngine();
        else ctx.audio.startEngineInstant();
    }

    static bool textFocused()
    {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        return dynamic_cast<juce::TextEditor*> (f) != nullptr;
    }

    bool keyPressed (const juce::KeyPress& k, juce::Component*) override
    {
        const auto mods = k.getModifiers();
        const int code = k.getKeyCode();
        if (mods.isCommandDown())
        {
            if (code == 'S' && mods.isShiftDown()) { ctx.saveAsDialog(); return true; }
            if (code == 'S') { ctx.save(); return true; }
            if (code == 'O') { ctx.openDialog(); return true; }
            if (code == 'N') { showPage ("home"); return true; }
            if (code == 'Z' && mods.isShiftDown()) { ctx.doc.redo(); return true; }
            if (code == 'Z') { ctx.doc.undo(); return true; }
            if (code == 'Y') { ctx.doc.redo(); return true; }
        }
        if (textFocused()) return false;
        if (code == juce::KeyPress::spaceKey) { toggleEngine(); return true; }
        if (code == 'E' && ! mods.isAnyModifierKeyDown()) { ctx.audio.shiftUpCount.fetch_add (1); return true; }
        if (code == 'Q' && ! mods.isAnyModifierKeyDown()) { ctx.audio.shiftDownCount.fetch_add (1); return true; }
        if (code >= '0' && code <= '9' && ! mods.isAnyModifierKeyDown())
        {
            ctx.audio.throttle.store (static_cast<float> (code - '0') / 10.0f);
            return true;
        }
        for (int i = 0; i < pageIds.size() && i < 10; ++i)
            if (code == juce::KeyPress::F1Key + i) { showPage (pageIds[i]); return true; }
        return false;
    }

    void timerCallback() override
    {
        // hold Up arrow = full throttle (released = closed)
        const bool up = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey) && hasKeyboardFocus (true) && ! textFocused();
        if (up != upHeld)
        {
            upHeld = up;
            ctx.audio.throttle.store (up ? 1.0f : 0.0f);
        }
        const bool down = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey) && hasKeyboardFocus (true) && ! textFocused();
        if (down != downHeld)
        {
            downHeld = down;
            ctx.audio.brake.store (down ? 1.0f : 0.0f);
        }
        const bool on = ctx.audio.ignition.load();
        engineBtn.setButtonText (on ? "Stop engine" : "Start engine");
        engineBtn.setToggleState (on, juce::dontSendNotification);
        if (++cpuTick % 15 == 0)
            cpu.setText ("CPU " + juce::String (ctx.audio.cpuLoad() * 100.0f, 1) + " %", juce::dontSendNotification);
        if (++statusAge == 30 * 12) status.setColour (juce::Label::textColourId, colours::textDim);
    }

    Theme theme;
    AppContext ctx;
    juce::OwnedArray<Page> pages;
    juce::StringArray pageIds;
    juce::OwnedArray<juce::TextButton> navButtons;
    juce::Label projectTitle, status, cpu;
    juce::TextButton undoBtn { "Undo" }, redoBtn { "Redo" }, saveBtn { "Save" }, saveAsBtn { "Save as..." }, engineBtn { "Start engine" };
    juce::Slider volume;
    juce::TooltipWindow tooltips { this, 600 };
    bool upHeld = false, downHeld = false;
    int cpuTick = 0, statusAge = 0;
};
} // namespace esd::ui

class EngineSoundDesignerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        window = std::make_unique<MainWindow>();
        if (commandLine.contains ("--snapshots"))
        {
            const auto dir = commandLine.fromFirstOccurrenceOf ("--snapshots", false, false).trim().unquoted();
            window->main->runSnapshots (juce::File (dir));
            return;
        }
        const auto arg = commandLine.unquoted().trim();
        if (arg.endsWithIgnoreCase (".esdproj") && juce::File::isAbsolutePath (arg))
            window->main->context().openFile (juce::File (arg));
    }

    void shutdown() override { window = nullptr; }

    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->main->context().confirmDiscard ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow() : DocumentWindow ("Engine Sound Designer", esd::ui::colours::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            main = new esd::ui::MainComponent();
            setContentOwned (main, true);
            setResizable (true, true);
            setResizeLimits (1100, 700, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        esd::ui::MainComponent* main = nullptr;
    };

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (EngineSoundDesignerApplication)
