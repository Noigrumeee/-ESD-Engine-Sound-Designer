#include "Visualizers.h"
#include "Theme.h"
#include "base/Math.h"

namespace esd::ui
{
// =====================================================================  FiringDiagram
void FiringDiagram::setPlan (const FiringPlan& p, int b)
{
    plan = p;
    banks = b;
    repaint();
}

void FiringDiagram::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Firing events (computed)");
    auto area = getLocalBounds().reduced (10).withTrimmedTop (18);
    if (! plan.valid)
    {
        g.setColour (colours::bad);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (plan.error, area, juce::Justification::centred, 4);
        return;
    }
    auto timeline = area.removeFromBottom (juce::jmin (70, area.getHeight() / 3));
    auto circleArea = area.toFloat();
    const float r = juce::jmin (circleArea.getWidth(), circleArea.getHeight()) * 0.5f - 18.0f;
    const auto c = circleArea.getCentre();

    g.setColour (colours::outline);
    g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.5f);
    g.setFont (uiFont (11.0f));
    g.setColour (colours::textDim);
    g.drawText (juce::String (juce::roundToInt (plan.cycleDeg)) + " deg cycle", juce::Rectangle<float> (c.x - 60, c.y - 8, 120, 16), juce::Justification::centred);

    auto pos = [&] (double angle, float radius)
    {
        const double a = angle / plan.cycleDeg * kTwoPi - kPi / 2.0;
        return juce::Point<float> (c.x + radius * static_cast<float> (std::cos (a)), c.y + radius * static_cast<float> (std::sin (a)));
    };
    for (std::size_t i = 0; i < plan.events.size(); ++i)
    {
        const auto& e = plan.events[i];
        const auto col = e.bank == 0 ? colours::bankA : colours::bankB;
        const auto p = pos (e.angleDeg, r);
        g.setColour (col);
        g.fillEllipse (p.x - 9, p.y - 9, 18, 18);
        g.setColour (juce::Colours::black);
        g.setFont (uiFont (11.0f, true));
        g.drawText (juce::String (e.cylinder + 1), juce::Rectangle<float> (p.x - 9, p.y - 9, 18, 18), juce::Justification::centred);
        // interval label between this event and the next
        const double mid = e.angleDeg + plan.intervals[i] * 0.5;
        const auto lp = pos (mid, r - 22.0f);
        g.setColour (colours::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText (juce::String (juce::roundToInt (plan.intervals[i])), juce::Rectangle<float> (lp.x - 16, lp.y - 7, 32, 14), juce::Justification::centred);
    }
    if (crank >= 0.0f)
    {
        g.setColour (colours::text.withAlpha (0.8f));
        g.drawLine (juce::Line<float> (c, pos (crank, r + 6)), 1.5f);
    }

    // timeline per bank
    const int rowsN = juce::jmax (1, banks);
    const float rowH = static_cast<float> (timeline.getHeight()) / static_cast<float> (rowsN);
    for (int b = 0; b < rowsN; ++b)
    {
        auto row = timeline.toFloat().withTop (timeline.getY() + rowH * static_cast<float> (b)).withHeight (rowH - 4);
        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (row, 3.0f);
        for (auto& e : plan.events)
        {
            if (banks > 1 && e.bank != b) continue;
            const float x = row.getX() + static_cast<float> (e.angleDeg / plan.cycleDeg) * row.getWidth();
            g.setColour (e.bank == 0 ? colours::bankA : colours::bankB);
            g.fillRect (x, row.getY() + 2, 3.0f, row.getHeight() - 4);
        }
        g.setColour (colours::textDim);
        g.setFont (uiFont (10.0f));
        g.drawText (banks > 1 ? (b == 0 ? "bank A" : "bank B") : "all", row.reduced (4, 0), juce::Justification::centredRight);
    }
}

// =====================================================================  ExhaustDiagram
void ExhaustDiagram::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Exhaust system (simplified schematic)");
    const auto& x = proj.exhaust;
    const auto* layout = findLayout (proj.engine.layoutId);
    const int cyl = layout ? layout->cylinders : 4;
    const bool dual = x.routing != ExhaustRouting::Single;
    auto area = getLocalBounds().reduced (14).withTrimmedTop (20);
    auto info = area.removeFromBottom (78);
    const float H = static_cast<float> (area.getHeight());
    const float W = static_cast<float> (area.getWidth());
    const float x0 = static_cast<float> (area.getX()), y0 = static_cast<float> (area.getY());
    const int lines = dual ? 2 : 1;

    // proportional lengths
    const double total = x.primaryLengthMm + x.midpipeLengthMm + x.tailpipeLengthMm + 900.0;
    auto px = [&] (double mm) { return static_cast<float> (mm / total) * (W - 90.0f); };

    // engine block
    const float blockW = 60.0f;
    juce::Rectangle<float> block (x0, y0 + H * 0.15f, blockW, H * 0.7f);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (block, 5.0f);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawFittedText (layout ? juce::String (layout->name) : juce::String(), block.toNearestInt().reduced (2), juce::Justification::centred, 3);

    const float pipeScale = 0.18f;
    for (int line = 0; line < lines; ++line)
    {
        const float cy = lines == 1 ? y0 + H * 0.5f : y0 + H * (line == 0 ? 0.3f : 0.7f);
        float cx = block.getRight();
        // primaries
        const int per = lines == 1 ? cyl : (cyl + 1) / 2;
        const float primEnd = cx + px (x.primaryLengthMm);
        for (int i = 0; i < per; ++i)
        {
            const float sy = block.getY() + block.getHeight() * (static_cast<float> (i) + 0.5f) / static_cast<float> (per);
            const float y = lines == 1 ? sy : (line == 0 ? juce::jmin (sy, y0 + H * 0.5f - 4) : juce::jmax (sy, y0 + H * 0.5f + 4));
            const float extra = static_cast<float> (x.unequalLength) * 18.0f * (static_cast<float> ((i * 5 + 3) % juce::jmax (1, per)) / juce::jmax (1, per) - 0.5f);
            g.setColour (colours::accent.withAlpha (0.8f));
            juce::Path p;
            p.startNewSubPath (cx, y);
            p.cubicTo (cx + px (x.primaryLengthMm) * 0.5f + extra, y, cx + px (x.primaryLengthMm) * 0.5f, cy, primEnd, cy);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, static_cast<float> (x.primaryDiameterMm) * pipeScale * 0.6f)));
        }
        cx = primEnd;
        const float pipeW = juce::jmax (2.0f, static_cast<float> (x.pipeDiameterMm) * pipeScale);
        auto pipeTo = [&] (float toX)
        {
            g.setColour (colours::textDim);
            g.fillRect (juce::Rectangle<float> (cx, cy - pipeW * 0.5f, toX - cx, pipeW));
            cx = toX;
        };
        const float midLen = px (x.midpipeLengthMm);
        auto box = [&] (float w, float h, juce::Colour col, const juce::String& label)
        {
            juce::Rectangle<float> r (cx, cy - h * 0.5f, w, h);
            g.setColour (col.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (col);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
            g.setFont (uiFont (10.0f));
            g.drawFittedText (label, r.toNearestInt(), juce::Justification::centred, 2);
            cx += w;
        };
        if (proj.boost.type == BoostType::Turbo || proj.boost.type == BoostType::TwinTurbo || proj.boost.type == BoostType::Twincharged)
            if (line == 0 || proj.boost.type == BoostType::TwinTurbo) box (26, 26, colours::accent2, "T");
        if (x.catalyst != CatalystType::None) { pipeTo (cx + midLen * 0.15f); box (34, 22, colours::warn, "CAT"); }
        if (x.resonatorEnabled)
        {
            pipeTo (cx + midLen * 0.3f);
            // side branch
            g.setColour (colours::good);
            const float bl = juce::jmin (H * 0.18f, static_cast<float> (x.resonatorLengthMm) * 0.04f);
            g.fillRect (juce::Rectangle<float> (cx - 2, line == 0 && lines == 2 ? cy - bl : cy, 4, bl));
            g.setFont (uiFont (10.0f));
            g.drawText ("res", juce::Rectangle<float> (cx - 20, (line == 0 && lines == 2) ? cy - bl - 14 : cy + bl, 40, 12), juce::Justification::centred);
        }
        pipeTo (cx + midLen * 0.35f);
        if (dual && (x.routing == ExhaustRouting::DualXPipe || x.routing == ExhaustRouting::DualHPipe) && line == 1)
        {
            g.setColour (colours::text);
            const float yA = y0 + H * 0.3f, yB = y0 + H * 0.7f;
            if (x.routing == ExhaustRouting::DualXPipe)
            {
                g.drawLine (cx - 20, yA, cx + 20, yB, 3.0f);
                g.drawLine (cx - 20, yB, cx + 20, yA, 3.0f);
            }
            else
                g.drawLine (cx, yA, cx, yB, 3.0f);
        }
        if (x.muffler != MufflerType::None)
        {
            const float mw = 30.0f + static_cast<float> (x.mufflerVolumeL) * 2.0f;
            const float mh = juce::jmin (H * (lines == 1 ? 0.5f : 0.3f), 16.0f + static_cast<float> (x.mufflerVolumeL) * 1.4f);
            box (mw, mh, colours::accent2, x.muffler == MufflerType::StraightThrough ? "straight" : x.muffler == MufflerType::Chambered ? "chambered" : "turbo");
        }
        pipeTo (cx + px (x.tailpipeLengthMm));
        // tips
        const int tips = juce::jmax (1, lines == 1 ? x.outlets : (x.outlets + 1) / 2);
        for (int t = 0; t < tips; ++t)
        {
            const float ty = cy + (static_cast<float> (t) - (tips - 1) * 0.5f) * (static_cast<float> (x.tailpipeDiameterMm) * pipeScale + 4.0f);
            g.setColour (colours::text);
            g.drawEllipse (cx, ty - static_cast<float> (x.tailpipeDiameterMm) * pipeScale * 0.5f, 8.0f, static_cast<float> (x.tailpipeDiameterMm) * pipeScale, 1.5f);
        }
    }

    // derived acoustic numbers (approximations, see docs/MODEL.md)
    const double cHot = speedOfSound (800.0 * 0.8), cTail = speedOfSound (480.0);
    const double midF = cHot / (4.0 * x.midpipeLengthMm * 1e-3);
    const double headF = speedOfSound (800.0) / (4.0 * x.primaryLengthMm * 1e-3);
    const double resF = x.resonatorEnabled ? cTail / (4.0 * x.resonatorLengthMm * 1e-3) : 0.0;
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    juce::String s;
    s << "Derived (approximations at ~800 K exhaust gas):  header 1/4-wave " << juce::String (juce::roundToInt (headF)) << " Hz   |   mid pipe 1/4-wave " << juce::String (juce::roundToInt (midF)) << " Hz";
    if (resF > 0) s << "   |   resonator notch " << juce::String (juce::roundToInt (resF)) << " Hz (+" << juce::String (juce::roundToInt (resF * 3)) << ")";
    s << "\nThe pipe resonances move with the exhaust temperature (speed of sound) in the simulation.";
    s << "\nNot a CFD / gas-dynamics simulation: 1D waveguides + filters, see the Model notes.";
    g.drawFittedText (s, info, juce::Justification::topLeft, 4);
}

// =====================================================================  AnalyzerPanel
// JUCE only notifies the component whose own visibility changed, not its children: the
// timers below always run and skip their work while the panel is not on screen.
AnalyzerPanel::AnalyzerPanel (AppContext& c, int f) : ctx (c), flags (f) { startTimerHz (30); }

void AnalyzerPanel::visibilityChanged() {}

void AnalyzerPanel::resized()
{
    auto r = getLocalBounds().reduced (8).withTrimmedTop (18);
    const int n = ((flags & Spectrum) ? 1 : 0) + ((flags & Spectrogram) ? 1 : 0) + ((flags & Scope) ? 1 : 0);
    if (n == 0) return;
    if (flags & Scope) scopeArea = r.removeFromBottom (n == 1 ? r.getHeight() : juce::jmax (60, r.getHeight() / 5));
    if ((flags & Spectrum) && (flags & Spectrogram))
    {
        specArea = r.removeFromTop (r.getHeight() * 55 / 100);
        sonoArea = r.withTrimmedTop (6);
    }
    else if (flags & Spectrum) specArea = r;
    else if (flags & Spectrogram) sonoArea = r;
    if (! sonoArea.isEmpty()) sonogram = juce::Image (juce::Image::RGB, juce::jmax (8, sonoArea.getWidth()), juce::jmax (8, sonoArea.getHeight()), true);
}

float AnalyzerPanel::xForFreq (float f, juce::Rectangle<float> a) const
{
    const float lo = std::log10 (20.0f), hi = std::log10 (16000.0f);
    return a.getX() + (std::log10 (juce::jmax (20.0f, f)) - lo) / (hi - lo) * a.getWidth();
}

void AnalyzerPanel::timerCallback()
{
    if (! isShowing()) return;
    const int got = ctx.audio.readAnalysis (scratch.data(), kSize);
    if (got > 0)
    {
        std::memmove (ring.data(), ring.data() + got, static_cast<std::size_t> (kSize - got) * sizeof (float));
        std::memcpy (ring.data() + (kSize - got), scratch.data(), static_cast<std::size_t> (got) * sizeof (float));
    }
    const auto t = ctx.audio.telemetry();
    rpm = t.rpm;
    firingHz = t.firingHz;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (ring.begin(), ring.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), kSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const float norm = 4.0f / kSize;
    for (int i = 0; i < kSize / 2; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[static_cast<std::size_t> (i)] * norm, -140.0f);
        auto& s = specDb[static_cast<std::size_t> (i)];
        s = db > s ? db : s * 0.7f + db * 0.3f; // fast attack, smoothed release
    }

    // dominant peaks 25 Hz .. 10 kHz
    dominant.clear();
    const double fs = ctx.audio.sampleRate();
    const double binHz = fs / kSize;
    std::vector<std::pair<float, float>> cand;
    for (int i = juce::jmax (2, static_cast<int> (25.0 / binHz)); i < juce::jmin (kSize / 2 - 2, static_cast<int> (10000.0 / binHz)); ++i)
    {
        const float a = specDb[static_cast<std::size_t> (i - 1)], b = specDb[static_cast<std::size_t> (i)], c = specDb[static_cast<std::size_t> (i + 1)];
        if (b > a && b >= c && b > -90.0f)
        {
            const float denom = a - 2 * b + c;
            const float off = std::abs (denom) > 1e-6f ? 0.5f * (a - c) / denom : 0.0f;
            cand.emplace_back (static_cast<float> ((i + off) * binHz), b);
        }
    }
    std::sort (cand.begin(), cand.end(), [] (auto& x, auto& y) { return x.second > y.second; });
    for (std::size_t i = 0; i < cand.size() && dominant.size() < 6; ++i)
    {
        bool near = false;
        for (auto& d : dominant)
            if (std::abs (d.first - cand[i].first) < 8.0f) near = true;
        if (! near) dominant.push_back (cand[i]);
    }

    // spectrogram column
    if (sonogram.isValid())
    {
        const int w = sonogram.getWidth(), h = sonogram.getHeight();
        sonogram.moveImageSection (0, 0, 1, 0, w - 1, h);
        for (int y = 0; y < h; ++y)
        {
            const float frac = 1.0f - static_cast<float> (y) / static_cast<float> (h);
            const float f = std::pow (10.0f, std::log10 (20.0f) + frac * (std::log10 (16000.0f) - std::log10 (20.0f)));
            const int bin = juce::jlimit (0, kSize / 2 - 1, static_cast<int> (f / binHz));
            const float v = juce::jlimit (0.0f, 1.0f, (specDb[static_cast<std::size_t> (bin)] + 100.0f) / 90.0f);
            const auto col = juce::Colour::fromHSV (0.7f - 0.7f * v, 0.85f, v, 1.0f);
            sonogram.setPixelAt (w - 1, y, col);
        }
    }
    repaint();
}

void AnalyzerPanel::paintSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto a = area.toFloat();
    g.setColour (colours::background);
    g.fillRect (a);
    g.setFont (uiFont (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (f, a);
        g.setColour (colours::outline.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.setColour (colours::textDim);
        g.drawText (f >= 1000 ? juce::String (juce::roundToInt (f / 1000)) + "k" : juce::String (juce::roundToInt (f)), juce::Rectangle<float> (x + 2, a.getBottom() - 12, 30, 12), juce::Justification::left);
    }
    for (int db = -100; db <= 0; db += 20)
    {
        const float y = a.getY() + (static_cast<float> (-db) / 100.0f) * a.getHeight();
        g.setColour (colours::outline.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (colours::textDim);
        g.drawText (juce::String (db), juce::Rectangle<float> (a.getX() + 2, y, 30, 12), juce::Justification::left);
    }
    // engine order markers: firing frequency harmonics
    if (firingHz > 1.0f)
        for (int k = 1; k <= 12; ++k)
        {
            const float f = firingHz * static_cast<float> (k);
            if (f > 16000.0f) break;
            const float x = xForFreq (f, a);
            g.setColour ((k == 1 ? colours::accent : colours::accent.withAlpha (0.35f)));
            g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        }

    const double binHz = ctx.audio.sampleRate() / kSize;
    juce::Path p;
    bool started = false;
    for (int i = 1; i < kSize / 2; ++i)
    {
        const float f = static_cast<float> (i * binHz);
        if (f < 20.0f || f > 16000.0f) continue;
        const float y = a.getY() + juce::jlimit (0.0f, 1.0f, -specDb[static_cast<std::size_t> (i)] / 100.0f) * a.getHeight();
        const float x = xForFreq (f, a);
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else p.lineTo (x, y);
    }
    g.setColour (colours::accent2);
    g.strokePath (p, juce::PathStrokeType (1.3f));

    // dominant frequency labels
    g.setFont (uiFont (11.0f));
    auto list = area.reduced (6).removeFromRight (190).removeFromTop (16 * 7).toFloat();
    g.setColour (colours::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (list, 4.0f);
    g.setColour (colours::text);
    g.drawText ("Dominant      Hz     order", list.removeFromTop (16).reduced (6, 0), juce::Justification::left);
    const float rev = rpm / 60.0f;
    for (auto& d : dominant)
    {
        juce::String s;
        s << juce::String (d.second, 1) << " dB   " << juce::String (d.first, 1);
        if (rev > 1.0f) s << "   " << juce::String (d.first / rev, 2);
        g.setColour (colours::textDim);
        g.drawText (s, list.removeFromTop (16).reduced (6, 0), juce::Justification::left);
    }
}

void AnalyzerPanel::paintScope (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto a = area.toFloat();
    g.setColour (colours::background);
    g.fillRect (a);
    // one engine cycle when running, otherwise 40 ms
    const double fs = ctx.audio.sampleRate();
    int n = static_cast<int> (fs * 0.04);
    if (rpm > 100.0f) n = juce::jlimit (64, kSize, static_cast<int> (fs * 2.0 * 60.0 / rpm));
    juce::Path p;
    for (int i = 0; i < n; ++i)
    {
        const float v = ring[static_cast<std::size_t> (kSize - n + i)];
        const float x = a.getX() + a.getWidth() * static_cast<float> (i) / static_cast<float> (n);
        const float y = a.getCentreY() - juce::jlimit (-1.0f, 1.0f, v * 2.0f) * a.getHeight() * 0.5f;
        i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
    }
    g.setColour (colours::good);
    g.strokePath (p, juce::PathStrokeType (1.2f));
    g.setColour (colours::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (rpm > 100.0f ? "waveform: 2 crank revolutions (x2 zoom)" : "waveform", a.reduced (4, 2), juce::Justification::topLeft);
}

void AnalyzerPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Analyzer (live output)");
    if (! specArea.isEmpty() && (flags & Spectrum)) paintSpectrum (g, specArea);
    if (! sonoArea.isEmpty() && (flags & Spectrogram) && sonogram.isValid())
    {
        g.drawImage (sonogram, sonoArea.toFloat());
        g.setColour (colours::textDim);
        g.setFont (uiFont (10.0f));
        g.drawText ("spectrogram 20 Hz - 16 kHz (log)", sonoArea.reduced (4, 2), juce::Justification::topLeft);
    }
    if (! scopeArea.isEmpty() && (flags & Scope)) paintScope (g, scopeArea);
}

// =====================================================================  LayerMixer
LayerMixer::LayerMixer (AppContext& c) : ctx (c)
{
    for (int i = 0; i < kNumLayers; ++i)
    {
        auto row = std::make_unique<Row>();
        row->name.setText (layerName (static_cast<LayerId> (i)), juce::dontSendNotification);
        row->name.setFont (uiFont (12.5f, true));
        row->mute.setClickingTogglesState (true);
        row->solo.setClickingTogglesState (true);
        row->mute.setColour (juce::TextButton::buttonOnColourId, colours::bad);
        row->solo.setColour (juce::TextButton::buttonOnColourId, colours::warn);
        row->gain.setSliderStyle (juce::Slider::LinearHorizontal);
        row->gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        row->gain.setRange (-60.0, 12.0, 0.1);
        row->gain.setTextValueSuffix (" dB");
        row->gain.setSkewFactorFromMidPoint (-12.0);
        row->pan.setSliderStyle (juce::Slider::LinearHorizontal);
        row->pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        row->pan.setRange (-1.0, 1.0, 0.01);
        row->pan.setTooltip ("Pan offset");
        row->pan.setDoubleClickReturnValue (true, 0.0);
        auto* r = row.get();
        const auto idx = static_cast<std::size_t> (i);
        auto commit = [this, idx, r] (const juce::String& what)
        {
            if (updating) return;
            ctx.doc.edit ([idx, r] (Project& p)
            {
                auto& l = p.mix.layers[idx];
                l.mute = r->mute.getToggleState();
                l.solo = r->solo.getToggleState();
                l.gainDb = static_cast<float> (r->gain.getValue());
                l.pan = static_cast<float> (r->pan.getValue());
            }, what + " " + layerName (static_cast<LayerId> (idx)), "mix." + juce::String (layerName (static_cast<LayerId> (idx))) + what, true);
        };
        row->mute.onClick = [commit] { commit ("mute"); };
        row->solo.onClick = [commit] { commit ("solo"); };
        row->gain.onValueChange = [commit] { commit ("gain"); };
        row->pan.onValueChange = [commit] { commit ("pan"); };
        for (juce::Component* comp : { static_cast<juce::Component*> (&row->name), static_cast<juce::Component*> (&row->mute),
                                       static_cast<juce::Component*> (&row->solo), static_cast<juce::Component*> (&row->gain),
                                       static_cast<juce::Component*> (&row->pan) })
            addAndMakeVisible (comp);
        rows[idx] = std::move (row);
    }
    master.setSliderStyle (juce::Slider::LinearHorizontal);
    master.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    master.setRange (-48.0, 12.0, 0.1);
    master.setTextValueSuffix (" dB");
    master.onValueChange = [this]
    {
        if (updating) return;
        const float v = static_cast<float> (master.getValue());
        ctx.doc.edit ([v] (Project& p) { p.mix.masterGainDb = v; }, "Master gain", "mix.master", true);
    };
    masterLabel.setText ("Master", juce::dontSendNotification);
    masterLabel.setFont (uiFont (13.5f, true));
    addAndMakeVisible (master);
    addAndMakeVisible (masterLabel);
    refresh();
    startTimerHz (20);
}

void LayerMixer::visibilityChanged() {}

void LayerMixer::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& m = ctx.doc.project().mix;
    for (int i = 0; i < kNumLayers; ++i)
    {
        auto& r = *rows[static_cast<std::size_t> (i)];
        const auto& l = m.layers[static_cast<std::size_t> (i)];
        r.mute.setToggleState (l.mute, juce::dontSendNotification);
        r.solo.setToggleState (l.solo, juce::dontSendNotification);
        r.gain.setValue (l.gainDb, juce::dontSendNotification);
        r.pan.setValue (l.pan, juce::dontSendNotification);
        const bool used = layerUsedBy (static_cast<LayerId> (i), static_cast<int> (ctx.doc.project().kind))
                          && (i != static_cast<int> (LayerId::Samples) || ! ctx.doc.project().samples.empty());
        for (juce::Component* comp : { static_cast<juce::Component*> (&r.name), static_cast<juce::Component*> (&r.mute), static_cast<juce::Component*> (&r.solo),
                                       static_cast<juce::Component*> (&r.gain), static_cast<juce::Component*> (&r.pan) })
            comp->setVisible (used);
    }
    master.setValue (m.masterGainDb, juce::dontSendNotification);
    resized();
    repaint();
}

void LayerMixer::timerCallback()
{
    if (! isShowing()) return;
    const auto t = ctx.audio.telemetry();
    for (int i = 0; i < kNumLayers; ++i)
    {
        auto& r = *rows[static_cast<std::size_t> (i)];
        const float target = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (t.layerRms[i], -80.0f) + 70.0f) / 70.0f);
        r.level = juce::jmax (target, r.level * 0.85f);
    }
    repaint();
}

void LayerMixer::resized()
{
    auto r = getLocalBounds().reduced (10).withTrimmedTop (20);
    int visible = 0;
    for (auto& row : rows) visible += row->name.isVisible() ? 1 : 0;
    const int rowH = juce::jmin (32, r.getHeight() / (visible + 1));
    for (auto& row : rows)
    {
        if (! row->name.isVisible()) continue;
        auto line = r.removeFromTop (rowH).reduced (0, 3);
        row->name.setBounds (line.removeFromLeft (112));
        row->mute.setBounds (line.removeFromLeft (26));
        line.removeFromLeft (3);
        row->solo.setBounds (line.removeFromLeft (26));
        line.removeFromLeft (6);
        line.removeFromRight (52); // meter
        row->pan.setBounds (line.removeFromRight (64));
        row->gain.setBounds (line.reduced (4, 0));
    }
    auto m = r.removeFromTop (rowH).reduced (0, 3);
    masterLabel.setBounds (m.removeFromLeft (173));
    m.removeFromRight (122);
    master.setBounds (m.reduced (4, 0));
}

void LayerMixer::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), "Layers (procedural sources)");
    for (auto& row : rows)
    {
        if (! row->name.isVisible()) continue;
        auto b = row->gain.getBounds();
        auto meter = juce::Rectangle<int> (getWidth() - 10 - 48, b.getY() + 6, 46, b.getHeight() - 12).toFloat();
        g.setColour (colours::panelRaised);
        g.fillRoundedRectangle (meter, 2.0f);
        g.setColour (row->level > 0.9f ? colours::bad : colours::good);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * row->level), 2.0f);
    }
}

// =====================================================================  Readout
void Readout::set (float v, int decimals, juce::Colour c)
{
    const auto newText = decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    if (newText == text && c == colour) return;
    value = v;
    text = newText;
    colour = c;
    repaint();
}

void Readout::setText (const juce::String& s, juce::Colour c)
{
    if (s == text && c == colour) return;
    text = s;
    colour = c;
    repaint();
}

void Readout::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (r, 5.0f);
    if (maxV > 0.0f)
    {
        g.setColour ((colour.isTransparent() ? colours::accent : colour).withAlpha (0.25f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, value / maxV)), 5.0f);
    }
    auto inner = r.reduced (8, 4);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f, true));
    g.drawText (t.toUpperCase(), inner.removeFromTop (14), juce::Justification::left);
    g.setColour (colour.isTransparent() ? colours::text : colour);
    g.setFont (uiFont (juce::jmin (24.0f, inner.getHeight() * 0.8f), true));
    g.drawFittedText (text + (u.isEmpty() ? "" : " " + u), inner.toNearestInt(), juce::Justification::left, 1, 0.5f);
}
} // namespace esd::ui
