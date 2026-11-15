#include "Page.h"

#include "model/Project.h"

namespace esd::ui
{
namespace
{
const juce::StringArray kCategories { "exhaust", "intake", "idle", "mechanical", "turbo", "transmission", "electric", "other" };

/// Waveform with a draggable selection and a play head.
class WaveformView : public juce::Component
{
public:
    void setBuffer (const juce::AudioBuffer<float>* b, double rate)
    {
        buffer = b;
        sampleRate = rate;
        rebuildPeaks();
        repaint();
    }
    void setSelection (int s, int e)
    {
        selStart = s;
        selEnd = e;
        repaint();
    }
    void setPlayhead (int sample)
    {
        if (sample != playhead) { playhead = sample; repaint(); }
    }
    int selectionStart() const { return juce::jmin (selStart, selEnd); }
    int selectionEnd() const { return juce::jmax (selStart, selEnd); }
    std::function<void()> onSelectionChanged;

    void resized() override { rebuildPeaks(); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds(), "Waveform");
        auto a = getLocalBounds().reduced (8).withTrimmedTop (18).toFloat();
        g.setColour (colours::background);
        g.fillRect (a);
        if (buffer == nullptr || buffer->getNumSamples() == 0)
        {
            g.setColour (colours::textDim);
            g.drawText ("Import a WAV or FLAC file, or select a sample", a, juce::Justification::centred);
            return;
        }
        const int n = buffer->getNumSamples();
        auto xOf = [&] (int s) { return a.getX() + a.getWidth() * static_cast<float> (s) / static_cast<float> (n); };
        if (selectionEnd() > selectionStart())
        {
            g.setColour (colours::accent.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (selectionStart()), a.getY(), xOf (selectionEnd()), a.getBottom()));
        }
        g.setColour (colours::accent2);
        const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
        for (std::size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = a.getX() + static_cast<float> (i);
            g.drawVerticalLine (juce::roundToInt (x), mid - peaks[i].second * half, mid - peaks[i].first * half + 1.0f);
        }
        if (playhead >= 0)
        {
            g.setColour (colours::text);
            g.drawVerticalLine (juce::roundToInt (xOf (playhead)), a.getY(), a.getBottom());
        }
        g.setColour (colours::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (juce::String (n / sampleRate, 3) + " s  -  selection " + juce::String (selectionStart() / sampleRate, 3) + " .. "
                        + juce::String (selectionEnd() / sampleRate, 3) + " s",
                    a.reduced (4, 2), juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! buffer) return;
        selStart = selEnd = sampleAt (e.position.x);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! buffer) return;
        selEnd = sampleAt (e.position.x);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (onSelectionChanged) onSelectionChanged();
    }

private:
    int sampleAt (float x) const
    {
        auto a = getLocalBounds().reduced (8).withTrimmedTop (18).toFloat();
        return juce::jlimit (0, buffer->getNumSamples(), static_cast<int> ((x - a.getX()) / a.getWidth() * static_cast<float> (buffer->getNumSamples())));
    }
    void rebuildPeaks()
    {
        peaks.clear();
        if (! buffer || buffer->getNumSamples() == 0) return;
        const int w = juce::jmax (1, getWidth() - 16);
        const int n = buffer->getNumSamples();
        peaks.resize (static_cast<std::size_t> (w));
        for (int x = 0; x < w; ++x)
        {
            const int s0 = static_cast<int> (static_cast<int64_t> (n) * x / w), s1 = juce::jmax (s0 + 1, static_cast<int> (static_cast<int64_t> (n) * (x + 1) / w));
            float lo = 0, hi = 0;
            for (int ch = 0; ch < buffer->getNumChannels(); ++ch)
            {
                auto r = buffer->findMinMax (ch, s0, s1 - s0);
                lo = juce::jmin (lo, r.getStart());
                hi = juce::jmax (hi, r.getEnd());
            }
            peaks[static_cast<std::size_t> (x)] = { lo, hi };
        }
    }
    const juce::AudioBuffer<float>* buffer = nullptr;
    double sampleRate = 48000.0;
    std::vector<std::pair<float, float>> peaks;
    int selStart = 0, selEnd = 0, playhead = -1;
};

class SampleEditorPage : public Page, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit SampleEditorPage (AppContext& c) : Page (c)
    {
        formats.registerBasicFormats();
        for (auto* b : { &importBtn, &removeBtn, &locateBtn, &playBtn, &stopBtn, &normalizeBtn, &trimBtn, &loopFindBtn })
            addAndMakeVisible (b);
        loopToggle.setButtonText ("Loop");
        addAndMakeVisible (loopToggle);
        importBtn.onClick = [this] { importFiles(); };
        removeBtn.onClick = [this] { removeSelected(); };
        locateBtn.onClick = [this] { locateSelected(); };
        playBtn.onClick = [this] { play(); };
        stopBtn.onClick = [this] { ctx.audio.stopClip(); };
        normalizeBtn.onClick = [this] { normalize(); };
        trimBtn.onClick = [this] { trimToSelection(); };
        loopFindBtn.onClick = [this] { findLoopPoint(); };
        normalizeBtn.setTooltip ("Non-destructive: sets the sample gain so the selection peaks at -1 dBFS");
        trimBtn.setTooltip ("Non-destructive: the sample region becomes the current selection");
        loopFindBtn.setTooltip ("Searches the loop end near the selection end that best matches the loop start (normalised cross-correlation on a zero crossing)");
        loopToggle.onClick = [this] { editSelected ([v = loopToggle.getToggleState()] (SampleRef& s) { s.loop = v; }, "Sample loop"); };

        list.setModel (this);
        list.setRowHeight (36);
        addAndMakeVisible (list);
        addAndMakeVisible (wave);
        wave.onSelectionChanged = [] {};

        for (auto* l : { &nameL, &catL, &rpmL, &loadL, &gainL, &info })
        {
            l->setFont (uiFont (13.0f));
            addAndMakeVisible (l);
        }
        nameL.setText ("Name", juce::dontSendNotification);
        catL.setText ("Category", juce::dontSendNotification);
        rpmL.setText ("Mapped rpm", juce::dontSendNotification);
        loadL.setText ("Mapped load", juce::dontSendNotification);
        gainL.setText ("Gain", juce::dontSendNotification);
        info.setColour (juce::Label::textColourId, colours::textDim);
        info.setJustificationType (juce::Justification::topLeft);
        name.onReturnKey = name.onFocusLost = [this] { editSelected ([t = name.getText().toStdString()] (SampleRef& s) { s.name = t; }, "Rename sample"); };
        addAndMakeVisible (name);
        category.addItemList (kCategories, 1);
        category.onChange = [this] { editSelected ([t = category.getText().toStdString()] (SampleRef& s) { s.category = t; }, "Sample category"); };
        addAndMakeVisible (category);
        auto num = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
            s.setRange (lo, hi, step);
            s.setTextValueSuffix (suffix);
            addAndMakeVisible (s);
        };
        num (rpm, 0, 20000, 10, " rpm");
        num (load, 0, 1, 0.01, "");
        num (gain, -48, 24, 0.1, " dB");
        rpm.onValueChange = [this] { if (! updating) editSelected ([v = rpm.getValue()] (SampleRef& s) { s.rpm = v; }, "Sample rpm", "sample.rpm"); };
        load.onValueChange = [this] { if (! updating) editSelected ([v = load.getValue()] (SampleRef& s) { s.load = v; }, "Sample load", "sample.load"); };
        gain.onValueChange = [this] { if (! updating) editSelected ([v = static_cast<float> (gain.getValue())] (SampleRef& s) { s.gainDb = v; }, "Sample gain", "sample.gain"); };
        hint.setText ("Samples are referenced, not copied: the project stores the file path and a non-destructive region / gain / mapping. "
                      "Mapping (rpm, load) is stored now and used by the sample-playback layer and the multidimensional interpolation of Phases 3 and 7.",
                      juce::dontSendNotification);
        hint.setFont (uiFont (12.0f));
        hint.setColour (juce::Label::textColourId, colours::textDim);
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
        projectChanged();
    }

    void visibilityChanged() override
    {
        if (isShowing()) startTimerHz (25);
        else stopTimer();
    }

    void projectChanged() override
    {
        list.updateContent();
        list.repaint();
        showSelected();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, listArea, "Project samples");
        drawPanel (g, propsArea, "Sample properties");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bar = r.removeFromTop (32);
        for (auto* b : std::initializer_list<juce::Component*> { &importBtn, &removeBtn, &locateBtn, &playBtn, &stopBtn, &loopToggle, &normalizeBtn, &trimBtn, &loopFindBtn })
        {
            b->setBounds (bar.removeFromLeft (b == &loopToggle ? 70 : 118).reduced (2, 0));
        }
        r.removeFromTop (10);
        listArea = r.removeFromLeft (320);
        list.setBounds (listArea.reduced (8).withTrimmedTop (20));
        r.removeFromLeft (12);
        propsArea = r.removeFromBottom (210);
        r.removeFromBottom (10);
        wave.setBounds (r);
        auto p = propsArea.reduced (10).withTrimmedTop (20);
        auto right = p.removeFromRight (p.getWidth() / 2);
        auto row = [&p] { auto x = p.removeFromTop (30).reduced (0, 2); return x; };
        auto lab = [] (juce::Rectangle<int> rr, juce::Label& l) { l.setBounds (rr.removeFromLeft (100)); return rr; };
        name.setBounds (lab (row(), nameL));
        category.setBounds (lab (row(), catL));
        rpm.setBounds (lab (row(), rpmL));
        load.setBounds (lab (row(), loadL));
        gain.setBounds (lab (row(), gainL));
        right.removeFromLeft (16);
        info.setBounds (right.removeFromTop (90));
        hint.setBounds (right);
    }

private:
    int selectedIndex() const
    {
        const int row = list.getSelectedRow();
        return row >= 0 && row < static_cast<int> (ctx.doc.project().samples.size()) ? row : -1;
    }

    juce::File resolve (const SampleRef& s) const
    {
        juce::File f (s.path);
        if (! juce::File::isAbsolutePath (s.path) && ctx.doc.file() != juce::File())
            f = ctx.doc.file().getParentDirectory().getChildFile (s.path);
        return f;
    }

    void editSelected (std::function<void (SampleRef&)> fn, const juce::String& what, const juce::String& key = {})
    {
        const int idx = selectedIndex();
        if (idx < 0) return;
        ctx.doc.edit ([idx, fn] (Project& p) { fn (p.samples[static_cast<std::size_t> (idx)]); }, what, key, true);
    }

    bool loadAudio (const SampleRef& s)
    {
        const auto f = resolve (s);
        if (f.getFullPathName() == loadedPath && audio.getNumSamples() > 0) return true;
        loadedPath = {};
        audio.setSize (0, 0);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (! reader) return false;
        const auto maxLen = static_cast<juce::int64> (reader->sampleRate * 600.0);
        const int len = static_cast<int> (juce::jmin (reader->lengthInSamples, maxLen));
        audio.setSize (static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels)), len);
        reader->read (&audio, 0, len, 0, true, audio.getNumChannels() > 1);
        audioRate = reader->sampleRate;
        loadedPath = f.getFullPathName();
        return true;
    }

    void showSelected()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const int idx = selectedIndex();
        for (auto* comp : std::initializer_list<juce::Component*> { &name, &category, &rpm, &load, &gain, &loopToggle, &playBtn, &normalizeBtn, &trimBtn, &loopFindBtn, &removeBtn, &locateBtn })
            comp->setEnabled (idx >= 0);
        if (idx < 0)
        {
            wave.setBuffer (nullptr, 48000.0);
            info.setText ({}, juce::dontSendNotification);
            return;
        }
        const auto& s = ctx.doc.project().samples[static_cast<std::size_t> (idx)];
        if (! name.hasKeyboardFocus (true)) name.setText (s.name, juce::dontSendNotification);
        category.setText (s.category, juce::dontSendNotification);
        rpm.setValue (s.rpm, juce::dontSendNotification);
        load.setValue (s.load, juce::dontSendNotification);
        gain.setValue (s.gainDb, juce::dontSendNotification);
        loopToggle.setToggleState (s.loop, juce::dontSendNotification);
        if (loadAudio (s))
        {
            wave.setBuffer (&audio, audioRate);
            const int st = static_cast<int> (s.startSec * audioRate);
            const int en = s.endSec > 0.0 ? static_cast<int> (s.endSec * audioRate) : audio.getNumSamples();
            if (wave.selectionEnd() == wave.selectionStart() || lastShown != idx) wave.setSelection (st, en);
            juce::String t;
            t << resolve (s).getFileName() << "\n" << juce::String (juce::roundToInt (audioRate)) << " Hz, " << audio.getNumChannels() << " ch, "
              << juce::String (audio.getNumSamples() / audioRate, 2) << " s\nRegion " << juce::String (s.startSec, 3) << " .. "
              << juce::String (s.endSec > 0 ? s.endSec : audio.getNumSamples() / audioRate, 3) << " s";
            info.setText (t, juce::dontSendNotification);
            info.setColour (juce::Label::textColourId, colours::textDim);
        }
        else
        {
            wave.setBuffer (nullptr, 48000.0);
            info.setText ("File missing or unreadable:\n" + resolve (s).getFullPathName() + "\nUse 'Locate...' to point to it.", juce::dontSendNotification);
            info.setColour (juce::Label::textColourId, colours::bad);
        }
        lastShown = idx;
    }

    void importFiles()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import audio samples", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.flac;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
        {
            std::vector<SampleRef> added;
            for (auto& f : fc.getResults())
            {
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
                if (! reader) { if (ctx.status) ctx.status ("Unsupported or unreadable file: " + f.getFileName(), true); continue; }
                SampleRef s;
                s.id = newUuid();
                s.name = f.getFileNameWithoutExtension().toStdString();
                s.path = f.getFullPathName().toStdString();
                s.category = "other";
                added.push_back (s);
            }
            if (added.empty()) return;
            ctx.doc.edit ([added] (Project& p) { p.samples.insert (p.samples.end(), added.begin(), added.end()); }, "Import samples", {}, true);
            list.selectRow (static_cast<int> (ctx.doc.project().samples.size()) - 1);
            if (ctx.status) ctx.status ("Imported " + juce::String (static_cast<int> (added.size())) + " sample(s)", false);
        });
    }

    void removeSelected()
    {
        const int idx = selectedIndex();
        if (idx < 0) return;
        ctx.audio.stopClip();
        ctx.doc.edit ([idx] (Project& p) { p.samples.erase (p.samples.begin() + idx); }, "Remove sample", {}, true);
    }

    void locateSelected()
    {
        chooser = std::make_unique<juce::FileChooser> ("Locate sample file", juce::File(), "*.wav;*.flac;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            editSelected ([p = fc.getResult().getFullPathName().toStdString()] (SampleRef& s) { s.path = p; }, "Locate sample");
        });
    }

    void play()
    {
        const int idx = selectedIndex();
        if (idx < 0 || audio.getNumSamples() == 0) return;
        const auto& s = ctx.doc.project().samples[static_cast<std::size_t> (idx)];
        auto clip = std::make_unique<PreviewClip>();
        clip->buffer.makeCopyOf (audio);
        clip->sampleRate = audioRate;
        clip->start = wave.selectionStart();
        clip->end = wave.selectionEnd() > wave.selectionStart() + 16 ? wave.selectionEnd() : audio.getNumSamples();
        clip->loop = loopToggle.getToggleState();
        clip->gain = juce::Decibels::decibelsToGain (s.gainDb);
        playStart = clip->start;
        ctx.audio.playClip (std::move (clip));
    }

    void normalize()
    {
        if (audio.getNumSamples() == 0) return;
        const int st = wave.selectionStart(), en = wave.selectionEnd() > st ? wave.selectionEnd() : audio.getNumSamples();
        float peak = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch) peak = juce::jmax (peak, audio.getMagnitude (ch, st, en - st));
        if (peak <= 1e-6f) return;
        const float db = -1.0f - juce::Decibels::gainToDecibels (peak);
        editSelected ([db] (SampleRef& s) { s.gainDb = db; }, "Normalize sample");
    }

    void trimToSelection()
    {
        if (wave.selectionEnd() <= wave.selectionStart()) return;
        const double a = wave.selectionStart() / audioRate, b = wave.selectionEnd() / audioRate;
        editSelected ([a, b] (SampleRef& s) { s.startSec = a; s.endSec = b; }, "Trim sample");
    }

    /// Real loop-point search: maximise the normalised cross-correlation between the window after
    /// the loop start and the window after candidate end points, restricted to rising zero crossings.
    void findLoopPoint()
    {
        if (audio.getNumSamples() == 0) return;
        const int st = wave.selectionStart();
        int en = wave.selectionEnd();
        if (en - st < static_cast<int> (audioRate * 0.05)) { if (ctx.status) ctx.status ("Select at least 50 ms first", true); return; }
        const float* x = audio.getReadPointer (0);
        const int win = juce::jmin (2048, (en - st) / 4);
        const int range = juce::jmin ((en - st) / 5, static_cast<int> (audioRate * 0.1));
        const int n = audio.getNumSamples();
        double best = -2.0;
        int bestPos = en;
        for (int cand = juce::jmax (st + win, en - range); cand <= juce::jmin (n - win - 1, en + range); ++cand)
        {
            if (! (x[cand - 1] <= 0.0f && x[cand] > 0.0f)) continue; // rising zero crossing
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < win; ++i)
            {
                const double a = x[st + i], b = x[cand + i];
                sxy += a * b; sxx += a * a; syy += b * b;
            }
            const double c = sxy / std::sqrt (juce::jmax (1e-12, sxx * syy));
            if (c > best) { best = c; bestPos = cand; }
        }
        wave.setSelection (st, bestPos);
        const double a = st / audioRate, b = bestPos / audioRate;
        editSelected ([a, b] (SampleRef& s) { s.startSec = a; s.endSec = b; s.loop = true; }, "Find loop point");
        if (ctx.status) ctx.status ("Loop end moved to " + juce::String (b, 4) + " s (correlation " + juce::String (best, 3) + ")", false);
    }

    void timerCallback() override
    {
        if (ctx.audio.isClipPlaying()) wave.setPlayhead (static_cast<int> (ctx.audio.clipPosition() * audioRate));
        else wave.setPlayhead (-1);
    }

    // ListBoxModel
    int getNumRows() override { return static_cast<int> (ctx.doc.project().samples.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& s = ctx.doc.project().samples[static_cast<std::size_t> (row)];
        if (sel) { g.setColour (colours::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
        const bool missing = ! resolve (s).existsAsFile();
        g.setColour (missing ? colours::bad : colours::text);
        g.setFont (uiFont (13.5f, true));
        g.drawText (s.name, 8, 2, w - 16, 18, juce::Justification::left);
        g.setColour (colours::textDim);
        g.setFont (uiFont (11.5f));
        juce::String sub = juce::String (s.category);
        if (s.rpm > 0) sub << "  -  " << juce::String (juce::roundToInt (s.rpm)) << " rpm, load " << juce::String (s.load, 2);
        if (s.loop) sub << "  -  loop";
        if (missing) sub << "  -  MISSING";
        g.drawText (sub, 8, 19, w - 16, 14, juce::Justification::left);
    }
    void selectedRowsChanged (int) override { showSelected(); }

    juce::AudioFormatManager formats;
    juce::AudioBuffer<float> audio;
    double audioRate = 48000.0;
    juce::String loadedPath;
    int lastShown = -1, playStart = 0;
    bool updating = false;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton importBtn { "Import..." }, removeBtn { "Remove" }, locateBtn { "Locate..." }, playBtn { "Play" }, stopBtn { "Stop" };
    juce::TextButton normalizeBtn { "Normalize" }, trimBtn { "Trim to sel." }, loopFindBtn { "Find loop point" };
    juce::ToggleButton loopToggle;
    juce::ListBox list;
    WaveformView wave;
    juce::Label nameL, catL, rpmL, loadL, gainL, info, hint;
    juce::TextEditor name;
    juce::ComboBox category;
    juce::Slider rpm, load, gain;
    juce::Rectangle<int> listArea, propsArea;
};
} // namespace

std::unique_ptr<Page> createSampleEditorPage (AppContext& c) { return std::make_unique<SampleEditorPage> (c); }
} // namespace esd::ui
