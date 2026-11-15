#include "Page.h"

#include "render/Render.h"

#include <thread>

namespace esd::ui
{
namespace
{
class ExportPage : public Page, private juce::Timer
{
public:
    explicit ExportPage (AppContext& c) : Page (c)
    {
        mode.addItem ("Dyno point (fixed rpm and load)", 1);
        mode.addItem ("Demo: idle, blip, full pull to limiter, lift-off", 2);
        mode.addItem ("Rpm ladder: one dyno render per rpm step", 3);
        mode.setSelectedId (1);
        mode.onChange = [this] { updateEnabled(); };
        rate.addItem ("48000 Hz", 1);
        rate.addItem ("96000 Hz", 2);
        rate.setSelectedId (1);
        format.addItem ("WAV 16-bit (TPDF dither)", 1);
        format.addItem ("WAV 24-bit", 2);
        format.addItem ("WAV 32-bit float", 3);
        format.addItem ("FLAC 24-bit", 4);
        format.setSelectedId (2);
        channels.addItem ("Stereo", 1);
        channels.addItem ("Mono", 2);
        channels.setSelectedId (1);
        auto num = [this] (juce::Slider& s, double lo, double hi, double step, double v, const juce::String& suffix)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 20);
            s.setRange (lo, hi, step);
            s.setValue (v);
            s.setTextValueSuffix (suffix);
            addAndMakeVisible (s);
        };
        num (rpm, 300, 20000, 10, 3000, " rpm");
        num (load, 0, 1, 0.01, 0.6, "");
        num (seconds, 0.5, 120, 0.5, 4, " s");
        num (rpmFrom, 300, 20000, 50, 1000, " rpm");
        num (rpmTo, 300, 20000, 50, 6000, " rpm");
        num (rpmStep, 50, 5000, 50, 500, " rpm");
        stems.setButtonText ("Also write one file per layer (stems)");
        for (auto* comp : std::initializer_list<juce::Component*> { &mode, &rate, &format, &channels, &stems, &renderBtn, &cancelBtn, &folderBtn, &openFolderBtn,
                                                                    &projectJsonBtn, &log })
            addAndMakeVisible (comp);
        const char* names[] = { "Mode", "Rpm", "Load", "Duration", "Ladder from", "Ladder to", "Ladder step", "Sample rate", "Format", "Channels" };
        for (auto* n : names)
        {
            auto* l = labels.add (new juce::Label ({}, n));
            l->setFont (uiFont (13.0f));
            addAndMakeVisible (l);
        }
        progress = std::make_unique<juce::ProgressBar> (progressValue);
        addAndMakeVisible (*progress);
        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setFont (uiFont (12.5f));
        renderBtn.onClick = [this] { startRender(); };
        cancelBtn.onClick = [this] { cancel.store (true); };
        folderBtn.onClick = [this] { chooseFolder(); };
        openFolderBtn.onClick = [this] { outDir.createDirectory(); outDir.startAsProcess(); };
        projectJsonBtn.onClick = [this] { ctx.exportProjectDialog(); };
        outDir = ctx.exportsDir;
        phaseNote.setText ("Renders use exactly the same simulation and synthesis as the live preview and are deterministic. "
                           "Seamless loop sets for games are made in the Loop Generator or directly with the game export below.",
                           juce::dontSendNotification);
        phaseNote.setFont (uiFont (12.5f));
        phaseNote.setColour (juce::Label::textColourId, colours::textDim);
        phaseNote.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (phaseNote);
        gamePanel = createGameExportPanel (ctx);
        addAndMakeVisible (*gamePanel);
        updateEnabled();
    }

    ~ExportPage() override
    {
        cancel.store (true);
        if (worker.joinable()) worker.join();
    }

    void projectChanged() override
    {
        const auto& e = ctx.doc.project().engine;
        rpmTo.setValue (juce::jmin (rpmTo.getValue(), e.limiterRpm), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, renderArea, "Render audio (offline, deterministic)");
        drawPanel (g, dataArea, "Project data");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        renderArea = r.removeFromLeft (juce::jmin (620, r.getWidth() * 55 / 100));
        r.removeFromLeft (12);
        dataArea = r;
        auto a = renderArea.reduced (12).withTrimmedTop (22);
        juce::Component* rowsC[] = { &mode, &rpm, &load, &seconds, &rpmFrom, &rpmTo, &rpmStep, &rate, &format, &channels };
        for (int i = 0; i < 10; ++i)
        {
            auto row = a.removeFromTop (30).reduced (0, 2);
            labels[i]->setBounds (row.removeFromLeft (110));
            rowsC[i]->setBounds (row);
        }
        stems.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        auto f = a.removeFromTop (30);
        folderBtn.setBounds (f.removeFromLeft (f.getWidth() / 2).reduced (2, 0));
        openFolderBtn.setBounds (f.reduced (2, 0));
        a.removeFromTop (6);
        auto b = a.removeFromTop (34);
        cancelBtn.setBounds (b.removeFromRight (100));
        b.removeFromRight (6);
        renderBtn.setBounds (b);
        a.removeFromTop (6);
        progress->setBounds (a.removeFromTop (22));
        a.removeFromTop (6);
        log.setBounds (a);

        auto d = dataArea.reduced (12).withTrimmedTop (22);
        projectJsonBtn.setBounds (d.removeFromTop (32));
        d.removeFromTop (12);
        phaseNote.setBounds (d.removeFromTop (70));
        d.removeFromTop (10);
        gamePanel->setBounds (d.removeFromTop (juce::jmin (d.getHeight(), 260)));
    }

private:
    void updateEnabled()
    {
        const int m = mode.getSelectedId();
        rpm.setEnabled (m == 1);
        load.setEnabled (m != 2);
        seconds.setEnabled (true);
        rpmFrom.setEnabled (m == 3);
        rpmTo.setEnabled (m == 3);
        rpmStep.setEnabled (m == 3);
        cancelBtn.setEnabled (busy);
        renderBtn.setEnabled (! busy);
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Output folder", outDir);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File()) outDir = fc.getResult();
            appendLog ("Output folder: " + outDir.getFullPathName());
        });
    }

    void appendLog (const juce::String& s)
    {
        log.moveCaretToEnd();
        log.insertTextAtCaret (s + "\n");
    }

    void startRender()
    {
        if (busy) return;
        if (worker.joinable()) worker.join();
        busy = true;
        cancel.store (false);
        progressValue = 0.0;
        updateEnabled();

        struct Job
        {
            std::string path;
            double rpm, load;
            bool demo;
        };
        std::vector<Job> jobs;
        const auto& p = ctx.doc.project();
        const auto base = juce::File::createLegalFileName (juce::String (p.name)).replaceCharacter (' ', '_');
        outDir.createDirectory();
        const int m = mode.getSelectedId();
        if (m == 1)
            jobs.push_back ({ outDir.getChildFile (base + "_" + juce::String (juce::roundToInt (rpm.getValue())) + "rpm_load" + juce::String (load.getValue(), 2) + ".wav").getFullPathName().toStdString(),
                              rpm.getValue(), load.getValue(), false });
        else if (m == 2)
            jobs.push_back ({ outDir.getChildFile (base + "_demo.wav").getFullPathName().toStdString(), 0, 0, true });
        else
            for (double v = rpmFrom.getValue(); v <= rpmTo.getValue() + 1e-6; v += rpmStep.getValue())
                jobs.push_back ({ outDir.getChildFile (base + "_" + juce::String (juce::roundToInt (v)) + "rpm.wav").getFullPathName().toStdString(), v, load.getValue(), false });

        const Project project = p; // copy: the worker never touches the live document
        const double sr = rate.getSelectedId() == 2 ? 96000.0 : 48000.0;
        const auto fmt = format.getSelectedId() == 1 ? WavFormat::Pcm16 : format.getSelectedId() == 3 ? WavFormat::Float32 : WavFormat::Pcm24;
        const bool flac = format.getSelectedId() == 4;
        auto* ctxPtr = &ctx;
        const bool mono = channels.getSelectedId() == 2;
        const bool withStems = stems.getToggleState();
        const double secs = seconds.getValue();
        appendLog ("Rendering " + juce::String (static_cast<int> (jobs.size())) + " file(s)...");
        startTimerHz (15);

        worker = std::thread ([this, jobs, project, sr, fmt, mono, withStems, secs, flac, ctxPtr]
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int done = 0;
            for (std::size_t j = 0; j < jobs.size() && ! cancel.load(); ++j)
            {
                const auto& job = jobs[j];
                RenderOptions o;
                o.sampleRate = sr;
                o.seconds = job.demo ? juce::jmax (10.0, secs) : secs;
                o.preRollSeconds = job.demo ? 0.0 : 1.5;
                o.cancel = &cancel;
                o.renderLayers = withStems;
                o.loaders = ctxPtr->audio.loaders;
                const auto jobCount = static_cast<double> (jobs.size());
                o.onProgress = [this, j, jobCount] (float f) { progressShared.store ((static_cast<double> (j) + f) / jobCount); };
                auto res = job.demo ? renderDemoPull (project, o) : renderDyno (project, job.rpm, job.load, o);
                if (res.cancelled) break;
                std::string err;
                auto write = [&] (const std::string& path, StereoBuffer& b) -> bool
                {
                    if (mono) for (std::size_t i = 0; i < b.frames(); ++i) b.left[i] = b.right[i] = 0.5f * (b.left[i] + b.right[i]);
                    if (! flac) return writeWav (path, b, fmt, mono, &err);
                    LoopSpec spec;
                    spec.flac = true;
                    std::string written;
                    return ctxPtr->writeAudio (path.substr (0, path.size() - 4), b, spec, written, &err);
                };
                if (! write (job.path, res.mix)) { pushMessage ("ERROR " + juce::String (err)); continue; }
                pushMessage ("wrote " + juce::String (job.path) + (flac ? " (as .flac)" : ""));
                if (withStems)
                    for (int l = 0; l < kNumLayers; ++l)
                    {
                        const auto stem = juce::String (job.path).dropLastCharacters (4) + "_" + layerName (static_cast<LayerId> (l)) + ".wav";
                        write (stem.toStdString(), res.layers[static_cast<std::size_t> (l)]);
                    }
                ++done;
            }
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            pushMessage (cancel.load() ? juce::String ("Cancelled.") : "Done: " + juce::String (done) + " file(s) in " + juce::String (ms / 1000.0, 2) + " s");
            finished.store (true);
        });
    }

    void pushMessage (const juce::String& s)
    {
        const juce::ScopedLock sl (msgLock);
        messages.add (s);
    }

    void timerCallback() override
    {
        progressValue = progressShared.load();
        juce::StringArray m;
        {
            const juce::ScopedLock sl (msgLock);
            m.swapWith (messages);
        }
        for (auto& s : m) appendLog (s);
        if (finished.exchange (false))
        {
            if (worker.joinable()) worker.join();
            busy = false;
            progressValue = 1.0;
            stopTimer();
            updateEnabled();
        }
    }

    juce::ComboBox mode, rate, format, channels;
    juce::Slider rpm, load, seconds, rpmFrom, rpmTo, rpmStep;
    juce::ToggleButton stems;
    juce::TextButton renderBtn { "Render" }, cancelBtn { "Cancel" }, folderBtn { "Output folder..." }, openFolderBtn { "Open output folder" };
    juce::TextButton projectJsonBtn { "Export project file (.esdproj JSON)..." };
    juce::OwnedArray<juce::Label> labels;
    juce::Label phaseNote;
    std::unique_ptr<juce::Component> gamePanel;
    juce::TextEditor log;
    std::unique_ptr<juce::ProgressBar> progress;
    double progressValue = 0.0;
    std::atomic<double> progressShared { 0.0 };
    std::atomic<bool> cancel { false }, finished { false };
    bool busy = false;
    std::thread worker;
    juce::CriticalSection msgLock;
    juce::StringArray messages;
    juce::File outDir;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> renderArea, dataArea;
};
} // namespace

std::unique_ptr<Page> createExportPage (AppContext& c) { return std::make_unique<ExportPage> (c); }
} // namespace esd::ui
