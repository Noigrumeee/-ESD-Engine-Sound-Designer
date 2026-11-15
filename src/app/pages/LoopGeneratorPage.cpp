#include "Page.h"

#include "render/AudioFile.h"
#include "render/GameExport.h"
#include "render/LoopGenerator.h"
#include "widgets/ParamEditors.h"

#include <thread>

namespace esd::ui
{
namespace
{
/// Runs loop generation / game exports on a worker thread with progress and cancel.
class LoopJob
{
public:
    std::atomic<bool> cancel { false }, finished { false };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection lock;
    juce::StringArray messages;
    LoopSetResult result;
    GameExportResult exportResult;
    std::thread worker;

    bool running() const { return worker.joinable() && ! finished.load(); }
    void join()
    {
        if (worker.joinable()) worker.join();
    }
    void push (const juce::String& m)
    {
        const juce::ScopedLock sl (lock);
        messages.add (m);
    }
    juce::StringArray take()
    {
        const juce::ScopedLock sl (lock);
        juce::StringArray m;
        m.swapWith (messages);
        return m;
    }
    ~LoopJob()
    {
        cancel.store (true);
        join();
    }
};

class LoopGeneratorPage : public Page, private juce::Timer, private juce::ListBoxModel
{
public:
    explicit LoopGeneratorPage (AppContext& c) : Page (c), column (c)
    {
        column.addPanel ("Grid (exported dimensions)", paramsInGroup ("loops", false));
        column.addPanel ("Format", paramsInGroup ("loops.format"));
        addAndMakeVisible (column);
        for (auto* b : { &folderBtn, &generateBtn, &cancelBtn, &openBtn, &playBtn, &stopBtn })
            addAndMakeVisible (b);
        folderBtn.onClick = [this] { chooseFolder(); };
        generateBtn.onClick = [this] { start(); };
        cancelBtn.onClick = [this] { job.cancel.store (true); };
        openBtn.onClick = [this] { outDir.createDirectory(); outDir.startAsProcess(); };
        playBtn.onClick = [this] { preview(); };
        stopBtn.onClick = [this] { ctx.audio.stopClip(); };
        resume.setButtonText ("Resume (keep loops already rendered)");
        resume.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (resume);
        summary.setMultiLine (true);
        summary.setReadOnly (true);
        summary.setFont (uiFont (13.0f));
        addAndMakeVisible (summary);
        progressBar = std::make_unique<juce::ProgressBar> (progressValue);
        addAndMakeVisible (*progressBar);
        list.setModel (this);
        list.setRowHeight (22);
        addAndMakeVisible (list);
        outDir = ctx.exportsDir.getChildFile ("Loops");
        updateButtons();
        projectChanged();
    }

    void projectChanged() override
    {
        column.refresh();
        const auto& p = ctx.doc.project();
        plan = planLoops (p);
        juce::String s;
        int rows = 0;
        std::string lastKey;
        for (auto& pt : plan.points)
        {
            const auto key = pt.set + pt.state + std::to_string (pt.load);
            if (key != lastKey) { ++rows; lastKey = key; }
        }
        s << "PLAN  " << static_cast<int> (plan.points.size()) << " loops in " << rows << " rows ("
          << (p.kind == PowertrainKind::Hybrid ? "engine set + motor set" : p.kind == PowertrainKind::Electric ? "motor set" : "engine set") << ")\n";
        s << "Audio  " << juce::String (plan.totalSeconds, 1) << " s, about " << juce::String (static_cast<double> (plan.estimatedBytes) / (1024.0 * 1024.0), 1) << " MB"
          << (p.loops.stems ? " including stems" : "") << "\n";
        s << "Exported dimensions  rpm x " << (p.kind == PowertrainKind::Electric ? "torque" : "load")
          << (p.loops.overrunState && p.kind != PowertrainKind::Electric ? " + deceleration row" : "")
          << (p.loops.regenState && p.kind != PowertrainKind::Thermal ? " + regeneration rows" : "") << "\n";
        s << "Interpolated at runtime  rpm between points (pitch + equal-power crossfade), load between rows (linear), "
             "throttle / gear / speed / boost through the game inputs.\n";
        s << "Loops hold whole engine cycles (the rendered rpm is adjusted by less than 1 %) and start on a cycle boundary.\n";
        s << "Output folder  " << outDir.getFullPathName();
        summary.setText (s, juce::dontSendNotification);
        if (! job.running()) list.updateContent();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        column.setBounds (r.removeFromLeft (juce::jmin (440, r.getWidth() * 38 / 100)));
        r.removeFromLeft (12);
        summary.setBounds (r.removeFromTop (130));
        r.removeFromTop (8);
        auto b = r.removeFromTop (30);
        const int w = b.getWidth() / 4;
        folderBtn.setBounds (b.removeFromLeft (w).reduced (2, 0));
        generateBtn.setBounds (b.removeFromLeft (w).reduced (2, 0));
        cancelBtn.setBounds (b.removeFromLeft (w).reduced (2, 0));
        openBtn.setBounds (b.reduced (2, 0));
        r.removeFromTop (6);
        resume.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        progressBar->setBounds (r.removeFromTop (22));
        r.removeFromTop (8);
        auto pb = r.removeFromBottom (30);
        playBtn.setBounds (pb.removeFromLeft (140).reduced (2, 0));
        stopBtn.setBounds (pb.removeFromLeft (100).reduced (2, 0));
        r.removeFromBottom (6);
        list.setBounds (r);
    }

private:
    void updateButtons()
    {
        const bool busy = job.running();
        generateBtn.setEnabled (! busy);
        cancelBtn.setEnabled (busy);
        folderBtn.setEnabled (! busy);
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Loop set folder", outDir);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) outDir = fc.getResult();
            projectChanged();
        });
    }

    void start()
    {
        if (job.running()) return;
        job.join();
        job.cancel.store (false);
        job.finished.store (false);
        job.progress.store (0.0f);
        const Project p = ctx.doc.project();
        const auto dir = outDir.getChildFile (juce::File::createLegalFileName (juce::String (p.name)));
        const bool keep = resume.getToggleState();
        auto* ctxPtr = &ctx;
        job.worker = std::thread ([this, p, dir, keep, ctxPtr]
        {
            LoopJobOptions o;
            o.outDir = dir.getFullPathName().toStdString();
            o.cancel = &job.cancel;
            o.resume = keep;
            o.loaders = ctxPtr->audio.loaders;
            o.writer = [ctxPtr] (const std::string& path, const StereoBuffer& b, const LoopSpec& s, std::string& w, std::string* e) { return ctxPtr->writeAudio (path, b, s, w, e); };
            o.progress = [this] (float f, const std::string& m) { job.progress.store (f); job.push (m); };
            job.result = generateLoops (p, o);
            job.finished.store (true);
        });
        startTimerHz (10);
        updateButtons();
    }

    void timerCallback() override
    {
        progressValue = job.progress.load();
        for (auto& m : job.take())
            if (ctx.status) ctx.status (m, false);
        if (job.finished.load())
        {
            job.join();
            stopTimer();
            const auto& r = job.result;
            shown = r.plan.points;
            resultRoot = juce::File (juce::String (juce::CharPointer_UTF8 (r.profilePath.c_str()))).getParentDirectory();
            list.updateContent();
            list.repaint();
            if (ctx.status)
                ctx.status (r.cancelled ? juce::String ("Loop generation cancelled (resume keeps what was rendered)")
                            : r.ok ? "Loop set ready: " + juce::String (static_cast<int> (r.plan.points.size())) + " loops in " + juce::String (r.renderSeconds, 1)
                                         + " s, normalisation " + juce::String (r.normalizeGainDb, 1) + " dB"
                                   : "Loop generation failed: " + juce::String (r.error),
                            ! r.ok && ! r.cancelled);
            updateButtons();
        }
    }

    void preview()
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= static_cast<int> (shown.size())) return;
        const auto f = resultRoot.getChildFile (juce::String (juce::CharPointer_UTF8 (shown[static_cast<std::size_t> (row)].file.c_str())));
        auto clip = std::make_unique<PreviewClip>();
        if (! ctx.decode (f, clip->buffer, clip->sampleRate)) return;
        clip->loop = true;
        clip->start = 0;
        clip->end = clip->buffer.getNumSamples();
        ctx.audio.stopEngine();
        ctx.audio.playClip (std::move (clip));
    }

    int getNumRows() override { return static_cast<int> (shown.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& pt = shown[static_cast<std::size_t> (row)];
        if (sel) { g.setColour (colours::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
        g.setFont (uiFont (12.5f));
        g.setColour (colours::text);
        juce::String s;
        s << pt.file << "    " << juce::String (pt.rpmExact, 1) << " rpm    load " << juce::String (pt.load, 2) << "    " << pt.cycles << " cycles    "
          << pt.loopSamples << " smp";
        g.drawText (s, 8, 0, w - 220, h, juce::Justification::centredLeft);
        const bool good = pt.seamJumpDb < 6.0;
        g.setColour (pt.reused ? colours::textDim : good ? colours::good : colours::warn);
        g.drawText (pt.reused ? juce::String ("kept") : "seam " + juce::String (pt.seamJumpDb, 1) + " dB  |  " + juce::String (pt.rmsDb, 1) + " dBFS", w - 220, 0, 212, h,
                    juce::Justification::centredRight);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { preview(); }

    ParamColumn column;
    juce::TextButton folderBtn { "Output folder..." }, generateBtn { "Generate loops" }, cancelBtn { "Cancel" }, openBtn { "Open folder" };
    juce::TextButton playBtn { "Preview loop" }, stopBtn { "Stop" };
    juce::ToggleButton resume;
    juce::TextEditor summary;
    std::unique_ptr<juce::ProgressBar> progressBar;
    double progressValue = 0.0;
    juce::ListBox list;
    LoopPlan plan;
    std::vector<LoopPoint> shown;
    juce::File outDir, resultRoot;
    std::unique_ptr<juce::FileChooser> chooser;
    LoopJob job;
};

/// Game engine package export (Godot / Unity / Unreal), used by the Export Studio.
class GameExportPanel : public juce::Component, private juce::Timer
{
public:
    explicit GameExportPanel (AppContext& c) : ctx (c)
    {
        target.addItem ("Godot 4.4+ (GDScript player + example, tested)", 1);
        target.addItem ("Unity (C# player)", 2);
        target.addItem ("Unreal Engine 5 (C++ component + DataTable CSV)", 3);
        target.addItem ("Generic (loops + JSON profile only)", 4);
        target.setSelectedId (1);
        for (auto* comp : std::initializer_list<juce::Component*> { &target, &exportBtn, &cancelBtn, &openBtn, &note })
            addAndMakeVisible (comp);
        progressBar = std::make_unique<juce::ProgressBar> (progressValue);
        addAndMakeVisible (*progressBar);
        exportBtn.onClick = [this] { start(); };
        cancelBtn.onClick = [this] { job.cancel.store (true); };
        openBtn.onClick = [this] { if (lastFolder.exists()) lastFolder.startAsProcess(); else ctx.exportsDir.startAsProcess(); };
        note.setText ("Renders the loop set of the Loop Generator settings (16-bit WAV) into a ready-to-use folder with the runtime profile "
                      "(engine_profile.json: grid, files, curves, transmission, events) and the playback code. The Godot package also contains "
                      "a project.godot, an example scene and tests/selftest.gd.",
                      juce::dontSendNotification);
        note.setFont (uiFont (12.5f));
        note.setColour (juce::Label::textColourId, colours::textDim);
        note.setJustificationType (juce::Justification::topLeft);
        cancelBtn.setEnabled (false);
    }
    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), "Game engine export"); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12).withTrimmedTop (22);
        target.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        auto b = r.removeFromTop (30);
        exportBtn.setBounds (b.removeFromLeft (b.getWidth() / 3).reduced (2, 0));
        cancelBtn.setBounds (b.removeFromLeft (b.getWidth() / 2).reduced (2, 0));
        openBtn.setBounds (b.reduced (2, 0));
        r.removeFromTop (6);
        progressBar->setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        note.setBounds (r);
    }

private:
    void start()
    {
        if (job.running()) return;
        job.join();
        job.cancel.store (false);
        job.finished.store (false);
        const Project p = ctx.doc.project();
        const auto t = static_cast<GameTarget> (target.getSelectedId() - 1);
        const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getFullPathName().toStdString();
        const auto tpl = findTemplatesDir (exeDir);
        if (tpl.empty()) { if (ctx.status) ctx.status ("Templates folder not found next to the application", true); return; }
        const auto parent = ctx.exportsDir.getFullPathName().toStdString();
        auto* ctxPtr = &ctx;
        job.worker = std::thread ([this, p, t, tpl, parent, ctxPtr]
        {
            LoopJobOptions o;
            o.cancel = &job.cancel;
            o.resume = false;
            o.loaders = ctxPtr->audio.loaders;
            o.progress = [this] (float f, const std::string&) { job.progress.store (f); };
            job.exportResult = exportGamePackage (p, t, parent, tpl, o);
            job.finished.store (true);
        });
        exportBtn.setEnabled (false);
        cancelBtn.setEnabled (true);
        startTimerHz (10);
    }
    void timerCallback() override
    {
        progressValue = job.progress.load();
        if (! job.finished.load()) return;
        job.join();
        stopTimer();
        const auto& r = job.exportResult;
        lastFolder = juce::File (juce::String (juce::CharPointer_UTF8 (r.folder.c_str())));
        if (ctx.status)
            ctx.status (r.cancelled ? juce::String ("Export cancelled") : r.ok ? "Exported to " + lastFolder.getFullPathName() : "Export failed: " + juce::String (r.error),
                        ! r.ok && ! r.cancelled);
        exportBtn.setEnabled (true);
        cancelBtn.setEnabled (false);
    }

    AppContext& ctx;
    juce::ComboBox target;
    juce::TextButton exportBtn { "Export package" }, cancelBtn { "Cancel" }, openBtn { "Open folder" };
    juce::Label note;
    std::unique_ptr<juce::ProgressBar> progressBar;
    double progressValue = 0.0;
    juce::File lastFolder;
    LoopJob job;
};
} // namespace

std::unique_ptr<Page> createLoopGeneratorPage (AppContext& c) { return std::make_unique<LoopGeneratorPage> (c); }
std::unique_ptr<juce::Component> createGameExportPanel (AppContext& c) { return std::make_unique<GameExportPanel> (c); }
} // namespace esd::ui
