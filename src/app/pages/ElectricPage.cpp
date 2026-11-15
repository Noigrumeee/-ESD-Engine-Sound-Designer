#include "Page.h"

#include "base/Math.h"
#include "widgets/CurveEditor.h"
#include "widgets/ParamEditors.h"
#include "widgets/Visualizers.h"

#include <numeric>

namespace esd::ui
{
namespace
{
/// Banner telling which powertrain the project uses, with a button to switch to this page's kind.
class KindBanner : public juce::Component
{
public:
    KindBanner (AppContext& c, PowertrainKind k, juce::String what) : ctx (c), kind (k), label (std::move (what))
    {
        use.onClick = [this]
        {
            const auto target = kind;
            ctx.doc.edit ([target] (Project& p)
            {
                p.kind = target;
                if (target == PowertrainKind::Electric && p.transmission.type != TransmissionType::EVFixed && p.transmission.type != TransmissionType::EVMultiSpeed)
                {
                    p.transmission.type = TransmissionType::EVFixed;
                    p.transmission.ratios = { 1.0 };
                    p.transmission.finalDrive = 9.0;
                }
            }, "Change powertrain");
        };
        addAndMakeVisible (use);
    }
    void refresh()
    {
        const bool active = ctx.doc.project().kind == kind;
        use.setVisible (! active);
        use.setButtonText ("Use " + label + " for this project");
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const bool active = ctx.doc.project().kind == kind;
        drawPanel (g, getLocalBounds(), {});
        g.setColour (active ? colours::good : colours::warn);
        g.setFont (uiFont (13.5f, true));
        g.drawText (active ? "This project is " + label + ": these parameters drive the simulation and the sound."
                           : "This project is " + juce::String (toString (ctx.doc.project().kind)) + ": the " + label + " parameters below are stored but not used.",
                    getLocalBounds().reduced (10, 0).withTrimmedRight (use.isVisible() ? 250 : 0), juce::Justification::centredLeft);
    }
    void resized() override { use.setBounds (getLocalBounds().reduced (6).removeFromRight (240)); }

private:
    AppContext& ctx;
    PowertrainKind kind;
    juce::String label;
    juce::TextButton use;
};

class ElectricPage : public Page
{
public:
    explicit ElectricPage (AppContext& c) : Page (c), banner (c, PowertrainKind::Electric, "electric"), column (c), analyzer (c, AnalyzerPanel::Spectrum), live (c)
    {
        column.addPanel ("Machine", paramsInGroup ("electric.machine"));
        column.addPanel ("Ratings", paramsInGroup ("electric.ratings"));
        column.addPanel ("Inverter & control", paramsInGroup ("electric.drive"));
        column.addPanel ("Cooling, bearings, structure", paramsInGroup ("electric.acoustic"));
        for (auto* comp : std::initializer_list<juce::Component*> { &banner, &column, &curve, &info, &analyzer, &live })
            addAndMakeVisible (comp);
        curve.editable = false;
        info.setMultiLine (true);
        info.setReadOnly (true);
        info.setFont (uiFont (13.0f));
        projectChanged();
    }

    void projectChanged() override
    {
        banner.refresh();
        column.refresh();
        const auto& m = ctx.doc.project().electric;
        std::vector<CurvePoint> pts;
        for (int i = 0; i <= 120; ++i)
        {
            const double rpm = m.maxRpm * i / 120.0;
            pts.push_back ({ rpm, m.torqueAt (rpm) });
        }
        curve.setCurve (Curve (pts, CurveInterp::Linear));
        curve.setRanges (0.0, m.maxRpm, 0.0, m.peakTorqueNm * 1.25);
        curve.setAxisLabels ("rpm", "Torque envelope (Nm, motoring = regeneration)", "Power (kW)");
        const ElectricSpec mc = m;
        curve.overlay = [mc] (double x) { return mc.torqueAt (x) * rpmToRadPerSec (x) / 1000.0; };
        curve.overlayMax = m.peakPowerKw * 1.25;
        curve.setMarkers ({ { m.baseRpm(), "base speed", colours::warn } });

        const double rev = m.maxRpm / 60.0, fe = rev * m.polePairs();
        juce::String s;
        s << "COMPUTED FROM THE MACHINE DATA\n";
        s << "Base speed  " << juce::String (juce::roundToInt (m.baseRpm())) << " rpm (constant torque below, field weakening above)\n";
        s << "Electrical frequency at max speed  " << juce::String (juce::roundToInt (fe)) << " Hz (" << juce::String (m.polePairs(), 1) << " pole pairs)\n";
        s << "Torque constant kt  " << juce::String (m.peakTorqueNm / std::max (1.0, m.maxCurrentA), 3) << " Nm/A\n";
        s << "Slot passing at max speed  " << juce::String (juce::roundToInt (m.slots * rev)) << " Hz (order " << m.slots << ")\n";
        if (m.type == ElectricMotorType::Induction)
            s << "Rotor bar passing  order " << m.rotorBars << ", slip at full torque " << juce::String (m.ratedSlip * 100.0, 1) << " %\n";
        else
            s << "Cogging order  lcm(slots, poles) = " << std::lcm (m.slots, m.poles) << "\n";
        s << "PWM  " << juce::String (juce::roundToInt (m.pwmHz)) << " Hz = " << juce::String (m.pwmHz / std::max (1.0, fe), 1) << " x fe max"
          << (m.randomPwm ? " (spread spectrum)" : "") << "\n";
        s << "Max electrical power  V x I x sqrt3 = " << juce::String (juce::roundToInt (m.voltage * m.maxCurrentA * 1.732 / 1000.0)) << " kW\n";
        s << "\nNot an electromagnetic finite element model: frequencies are exact consequences of the machine data, "
             "amplitudes are approximations (see docs/MODEL.md).";
        info.setText (s, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        banner.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        column.setBounds (r.removeFromLeft (juce::jmin (470, r.getWidth() * 42 / 100)));
        r.removeFromLeft (12);
        live.setBounds (r.removeFromBottom (48));
        r.removeFromBottom (8);
        auto top = r.removeFromTop (r.getHeight() * 45 / 100);
        info.setBounds (top.removeFromRight (top.getWidth() * 45 / 100));
        top.removeFromRight (8);
        curve.setBounds (top);
        r.removeFromTop (8);
        analyzer.setBounds (r);
    }

private:
    KindBanner banner;
    ParamColumn column;
    CurveEditor curve;
    juce::TextEditor info;
    AnalyzerPanel analyzer;
    LiveControls live;
};

/// Live vehicle driving controls shared by the hybrid and drivetrain pages.
class DriveControls : public juce::Component, private juce::Timer
{
public:
    explicit DriveControls (AppContext& c) : ctx (c)
    {
        vehicle.setButtonText ("Drive the vehicle");
        vehicle.onClick = [this]
        {
            ctx.audio.vehicleMode.store (vehicle.getToggleState());
            ctx.audio.dynoMode.store (false);
            if (vehicle.getToggleState()) ctx.audio.startEngineInstant();
        };
        up.onClick = [this] { ctx.audio.shiftUpCount.fetch_add (1); };
        down.onClick = [this] { ctx.audio.shiftDownCount.fetch_add (1); };
        for (auto* s : { &throttle, &brake })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
            s->setRange (0.0, 1.0, 0.01);
            addAndMakeVisible (s);
        }
        throttle.onValueChange = [this] { ctx.audio.throttle.store (static_cast<float> (throttle.getValue())); };
        brake.onValueChange = [this] { ctx.audio.brake.store (static_cast<float> (brake.getValue())); };
        tl.setText ("Throttle", juce::dontSendNotification);
        bl.setText ("Brake", juce::dontSendNotification);
        hint.setText ("Keys: Up = throttle, Down = brake, E / Q = gear up / down", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, colours::textDim);
        hint.setFont (uiFont (11.5f));
        for (auto* comp : std::initializer_list<juce::Component*> { &vehicle, &up, &down, &tl, &bl, &hint })
            addAndMakeVisible (comp);
        startTimerHz (15);
    }
    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), {}); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto row1 = r.removeFromTop (28);
        vehicle.setBounds (row1.removeFromLeft (160));
        down.setBounds (row1.removeFromRight (70));
        row1.removeFromRight (4);
        up.setBounds (row1.removeFromRight (70));
        hint.setBounds (row1.reduced (8, 0));
        r.removeFromTop (4);
        auto row2 = r.removeFromTop (26);
        tl.setBounds (row2.removeFromLeft (64));
        throttle.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 10));
        row2.removeFromLeft (16);
        bl.setBounds (row2.removeFromLeft (50));
        brake.setBounds (row2);
    }

private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        vehicle.setToggleState (ctx.audio.vehicleMode.load(), juce::dontSendNotification);
        throttle.setValue (ctx.audio.throttle.load(), juce::dontSendNotification);
        brake.setValue (ctx.audio.brake.load(), juce::dontSendNotification);
    }
    AppContext& ctx;
    juce::ToggleButton vehicle;
    juce::TextButton up { "Gear +" }, down { "Gear -" };
    juce::Slider throttle, brake;
    juce::Label tl, bl, hint;
};

/// Telemetry grid for vehicle / hybrid pages.
class VehicleReadouts : public juce::Component, private juce::Timer
{
public:
    VehicleReadouts (AppContext& c, bool hybridView) : ctx (c), hybrid (hybridView)
    {
        auto add = [this] (const char* t, const char* u, float mx) { addAndMakeVisible (tiles.add (new Readout (t, u, mx))); };
        add ("Speed", "km/h", 0);
        add ("Gear", "", 0);
        add ("Engine / input", "rpm", 0);
        add ("Clutch / SR", "%", 100);
        if (hybrid)
        {
            add ("Battery", "%", 100);
            add ("Battery power", "kW", 0);
            add ("Motor torque", "Nm", 0);
            add ("Mode", "", 0);
        }
        else
        {
            add ("Wheel power", "kW", 0);
            add ("Brake", "%", 100);
            add ("Throttle", "%", 100);
            add ("Shift", "", 0);
        }
        startTimerHz (20);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        const int w = r.getWidth() / 4, h = r.getHeight() / 2;
        for (int i = 0; i < tiles.size(); ++i) tiles[i]->setBounds (r.getX() + (i % 4) * w, r.getY() + (i / 4) * h, w, h);
    }

private:
    void timerCallback() override
    {
        if (! isShowing()) return;
        const auto t = ctx.audio.telemetry();
        const bool electric = ctx.doc.project().kind == PowertrainKind::Electric;
        tiles[0]->set (t.speedKmh, 0);
        tiles[1]->setText (t.gear <= 0 ? juce::String ("N") : juce::String (t.gear) + " / " + juce::String (t.numGears), t.shifting ? colours::warn : juce::Colour());
        tiles[2]->set (electric || (hybrid && ! t.engineOn) ? t.motorRpm : t.rpm, 0);
        const bool converter = ctx.doc.project().transmission.type == TransmissionType::TorqueConverter;
        tiles[3]->set ((converter ? t.converterSR : t.clutch) * 100.0f, 0);
        if (hybrid)
        {
            tiles[4]->set (t.soc * 100.0f, 1);
            tiles[5]->set (t.batteryKw, 1, t.batteryKw < 0 ? colours::good : juce::Colour());
            tiles[6]->set (t.motorTorque, 0);
            static const char* modes[] = { "EV", "ENGINE + MOTOR", "ENGINE CHARGING", "REGENERATION", "ENGINE ONLY" };
            tiles[7]->setText (modes[juce::jlimit (0, 4, t.hybridMode)], t.engineOn ? colours::accent : colours::good);
        }
        else
        {
            tiles[4]->set (electric ? t.motorPowerKw : t.powerKw, 1);
            tiles[5]->set (ctx.audio.brake.load() * 100.0f, 0);
            tiles[6]->set (ctx.audio.throttle.load() * 100.0f, 0);
            tiles[7]->setText (t.shifting ? "SHIFTING" : "-", t.shifting ? colours::warn : colours::textDim);
        }
    }
    AppContext& ctx;
    bool hybrid;
    juce::OwnedArray<Readout> tiles;
};

class HybridPage : public Page
{
public:
    explicit HybridPage (AppContext& c) : Page (c), banner (c, PowertrainKind::Hybrid, "hybrid"), column (c), drive (c), readouts (c, true),
                                          analyzer (c, AnalyzerPanel::Spectrum | AnalyzerPanel::Spectrogram)
    {
        column.addPanel ("Hybrid system", paramsInGroup ("hybrid"));
        note.setText ("The thermal engine is edited in the Engine Builder, the motor in the Electric Motor page, the gearbox and the "
                      "vehicle in Transmission & Vehicle. Series / power-split: the engine speed follows the power demand (generator). "
                      "Parallel / mild / plug-in: engine and motor share the gearbox input when the engine clutch is closed.",
                      juce::dontSendNotification);
        note.setFont (uiFont (12.5f));
        note.setColour (juce::Label::textColourId, colours::textDim);
        note.setJustificationType (juce::Justification::topLeft);
        for (auto* comp : std::initializer_list<juce::Component*> { &banner, &column, &note, &drive, &readouts, &analyzer })
            addAndMakeVisible (comp);
        projectChanged();
    }
    void projectChanged() override
    {
        banner.refresh();
        column.refresh();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        banner.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        auto left = r.removeFromLeft (juce::jmin (440, r.getWidth() * 40 / 100));
        note.setBounds (left.removeFromBottom (110));
        column.setBounds (left);
        r.removeFromLeft (12);
        drive.setBounds (r.removeFromTop (72));
        r.removeFromTop (8);
        readouts.setBounds (r.removeFromTop (130));
        r.removeFromTop (8);
        analyzer.setBounds (r);
    }

private:
    KindBanner banner;
    ParamColumn column;
    juce::Label note;
    DriveControls drive;
    VehicleReadouts readouts;
    AnalyzerPanel analyzer;
};

/// Speed per gear at the redline (computed from the ratios) - a quick sanity view.
class GearChart : public juce::Component
{
public:
    explicit GearChart (AppContext& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds(), "Speed at the redline per gear (computed)");
        const auto& p = ctx.doc.project();
        const auto& t = p.transmission;
        const double red = p.kind == PowertrainKind::Electric ? p.electric.maxRpm : p.engine.redlineRpm;
        auto a = getLocalBounds().reduced (12).withTrimmedTop (20).toFloat();
        std::vector<std::pair<juce::String, double>> bars;
        auto speed = [&] (double ratio) { return red / 60.0 * kTwoPi / (ratio * t.finalDrive) * p.vehicle.wheelRadiusM * 3.6; };
        if (t.type == TransmissionType::CVT)
        {
            bars.push_back ({ "CVT low", speed (t.cvtRatioMax) });
            bars.push_back ({ "CVT high", speed (t.cvtRatioMin) });
        }
        else
            for (std::size_t i = 0; i < t.ratios.size(); ++i)
                bars.push_back ({ "Gear " + juce::String (static_cast<int> (i) + 1), speed (t.ratios[i]) });
        double mx = 1.0;
        for (auto& b : bars) mx = std::max (mx, b.second);
        const float h = a.getHeight() / static_cast<float> (std::max<std::size_t> (1, bars.size()));
        g.setFont (uiFont (12.0f));
        for (std::size_t i = 0; i < bars.size(); ++i)
        {
            auto row = a.withY (a.getY() + h * static_cast<float> (i)).withHeight (h - 3);
            g.setColour (colours::textDim);
            g.drawText (bars[i].first, row.removeFromLeft (70), juce::Justification::centredLeft);
            g.setColour (colours::accent.withAlpha (0.7f));
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * static_cast<float> (bars[i].second / mx) * 0.85f), 3.0f);
            g.setColour (colours::text);
            g.drawText (juce::String (juce::roundToInt (bars[i].second)) + " km/h", row.reduced (6, 0), juce::Justification::centredLeft);
        }
    }

private:
    AppContext& ctx;
};

class DrivetrainPage : public Page
{
public:
    explicit DrivetrainPage (AppContext& c) : Page (c), column (c), drive (c), readouts (c, false), gears (c), analyzer (c, AnalyzerPanel::Spectrum | AnalyzerPanel::Scope)
    {
        column.addPanel ("Gearbox", paramsInGroup ("transmission", false));
        column.addPanel ("Shift logic & CVT", paramsInGroup ("transmission.logic"));
        column.addPanel ("Vehicle", paramsInGroup ("vehicle"));
        for (auto* comp : std::initializer_list<juce::Component*> { &column, &drive, &readouts, &gears, &analyzer })
            addAndMakeVisible (comp);
        projectChanged();
    }
    void projectChanged() override
    {
        column.refresh();
        gears.repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        column.setBounds (r.removeFromLeft (juce::jmin (460, r.getWidth() * 40 / 100)));
        r.removeFromLeft (12);
        drive.setBounds (r.removeFromTop (72));
        r.removeFromTop (8);
        readouts.setBounds (r.removeFromTop (130));
        r.removeFromTop (8);
        auto bottom = r;
        gears.setBounds (bottom.removeFromLeft (bottom.getWidth() * 38 / 100));
        bottom.removeFromLeft (8);
        analyzer.setBounds (bottom);
    }

private:
    ParamColumn column;
    DriveControls drive;
    VehicleReadouts readouts;
    GearChart gears;
    AnalyzerPanel analyzer;
};
} // namespace

std::unique_ptr<Page> createElectricPage (AppContext& c) { return std::make_unique<ElectricPage> (c); }
std::unique_ptr<Page> createHybridPage (AppContext& c) { return std::make_unique<HybridPage> (c); }
std::unique_ptr<Page> createDrivetrainPage (AppContext& c) { return std::make_unique<DrivetrainPage> (c); }
} // namespace esd::ui
