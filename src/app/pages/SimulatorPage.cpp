#include "Page.h"

#include "widgets/Visualizers.h"

namespace esd::ui
{
namespace
{
/// Strip chart of rpm / load / throttle over the last seconds.
class HistoryChart : public juce::Component
{
public:
    static constexpr int kLen = 300; // 10 s at 30 Hz
    void push (float rpmNorm, float load, float throttle)
    {
        rpm[static_cast<std::size_t> (pos)] = rpmNorm;
        ld[static_cast<std::size_t> (pos)] = load;
        th[static_cast<std::size_t> (pos)] = throttle;
        pos = (pos + 1) % kLen;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds(), "History (10 s)");
        auto a = getLocalBounds().reduced (10).withTrimmedTop (18).toFloat();
        auto line = [&] (const std::array<float, kLen>& v, juce::Colour c, float w)
        {
            juce::Path p;
            for (int i = 0; i < kLen; ++i)
            {
                const float val = v[static_cast<std::size_t> ((pos + i) % kLen)];
                const float x = a.getX() + a.getWidth() * static_cast<float> (i) / (kLen - 1);
                const float y = a.getBottom() - juce::jlimit (0.0f, 1.05f, val) * a.getHeight();
                i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w));
        };
        line (th, colours::textDim, 1.0f);
        line (ld, colours::accent2, 1.3f);
        line (rpm, colours::accent, 2.0f);
        g.setFont (uiFont (11.0f));
        g.setColour (colours::accent);
        g.drawText ("rpm / limiter", a.removeFromTop (14), juce::Justification::right);
        g.setColour (colours::accent2);
        g.drawText ("load", a.removeFromTop (14), juce::Justification::right);
        g.setColour (colours::textDim);
        g.drawText ("throttle", a.removeFromTop (14), juce::Justification::right);
    }

private:
    std::array<float, kLen> rpm {}, ld {}, th {};
    int pos = 0;
};

class SimulatorPage : public Page, private juce::Timer
{
public:
    explicit SimulatorPage (AppContext& c) : Page (c), analyzer (c, AnalyzerPanel::Spectrum | AnalyzerPanel::Spectrogram | AnalyzerPanel::Scope)
    {
        for (auto* r : readouts) addAndMakeVisible (r);
        addAndMakeVisible (analyzer);
        addAndMakeVisible (history);

        crank.setTooltip ("Hold the starter like a real key start (ignition on + starter until the engine catches)");
        crank.onClick = [this] { runScenario (Scenario::Crank); };
        instant.onClick = [this] { ctx.audio.dynoMode.store (false); ctx.audio.startEngineInstant(); };
        stop.onClick = [this] { scenario = Scenario::None; ctx.audio.stopEngine(); };
        limiter.setToggleState (true, juce::dontSendNotification);
        limiter.onClick = [this] { ctx.audio.limiterEnabled.store (limiter.getToggleState()); };
        dyno.onClick = [this] { ctx.audio.dynoMode.store (dyno.getToggleState()); if (dyno.getToggleState()) ctx.audio.vehicleMode.store (false); };
        vehicle.onClick = [this] { ctx.audio.vehicleMode.store (vehicle.getToggleState()); if (vehicle.getToggleState()) ctx.audio.dynoMode.store (false); };
        gearUp.onClick = [this] { ctx.audio.shiftUpCount.fetch_add (1); };
        gearDown.onClick = [this] { ctx.audio.shiftDownCount.fetch_add (1); };
        vehicle.setTooltip ("Connect the engine / motor to the gearbox and the vehicle (Transmission & Vehicle page). Keys E / Q shift, Down brakes.");
        for (auto* b : std::initializer_list<juce::Component*> { &crank, &instant, &stop, &limiter, &dyno, &vehicle, &gearUp, &gearDown })
            addAndMakeVisible (b);

        auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double step, const juce::String& tip)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
            s.setRange (lo, hi, step);
            s.setTooltip (tip);
            l.setText (name, juce::dontSendNotification);
            l.setFont (uiFont (13.0f));
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };
        setup (throttle, throttleL, "Throttle", 0, 1, 0.01, "Pedal position. Keyboard: hold Up arrow = full throttle, keys 0-9 = 0-90 %");
        setup (load, loadL, "External load", 0, 1, 0.01, "Load torque applied to the crank (fraction of peak torque): road, hill, dyno brake");
        setup (dynoRpm, dynoRpmL, "Dyno rpm", 500, 9000, 10, "Dyno mode holds this speed");
        setup (dynoLoad, dynoLoadL, "Dyno load", -1, 1, 0.01, "Dyno mode: cylinder filling (engine load) or motor torque (negative = regeneration)");
        setup (brakeS, brakeL, "Brake", 0, 1, 0.01, "Vehicle mode: brake pedal (Down arrow)");
        brakeS.onValueChange = [this] { ctx.audio.brake.store (static_cast<float> (brakeS.getValue())); };
        throttle.onValueChange = [this] { ctx.audio.throttle.store (static_cast<float> (throttle.getValue())); };
        load.onValueChange = [this] { ctx.audio.externalLoad.store (static_cast<float> (load.getValue())); };
        dynoRpm.onValueChange = [this] { ctx.audio.dynoRpm.store (static_cast<float> (dynoRpm.getValue())); };
        dynoLoad.onValueChange = [this] { ctx.audio.dynoLoad.store (static_cast<float> (dynoLoad.getValue())); };

        scenarioL.setText ("Scenarios", juce::dontSendNotification);
        scenarioL.setFont (uiFont (12.0f, true));
        scenarioL.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (scenarioL);
        const std::pair<const char*, Scenario> sc[] = { { "Rev to limiter", Scenario::RevLimiter }, { "Pull + lift-off", Scenario::LiftOff },
                                                        { "Rpm sweep (dyno)", Scenario::Sweep }, { "Load step", Scenario::LoadStep },
                                                        { "Traction loss", Scenario::Traction }, { "Hold 3000 rpm", Scenario::Hold } };
        for (auto& [name, s] : sc)
        {
            auto* b = scenarioButtons.add (new juce::TextButton (name));
            const auto sv = s;
            b->onClick = [this, sv] { runScenario (sv); };
            addAndMakeVisible (b);
        }
        modelNote.setText ("Simplified mean-value model: torque balance J dw/dt = T_ind - T_friction - T_pumping - T_load. "
                           "Temperatures are first-order illustrations. See docs/MODEL.md.",
                           juce::dontSendNotification);
        modelNote.setFont (uiFont (11.5f));
        modelNote.setColour (juce::Label::textColourId, colours::textDim);
        modelNote.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (modelNote);
    }

    void visibilityChanged() override
    {
        if (isShowing()) startTimerHz (30);
        else stopTimer();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto left = r.removeFromLeft (juce::jmin (520, r.getWidth() * 45 / 100));
        r.removeFromLeft (12);
        // readouts grid 4 x 3
        auto grid = left.removeFromTop (3 * 64);
        const int cw = grid.getWidth() / 4;
        for (int i = 0; i < readouts.size(); ++i)
            readouts[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * 64, cw, 62);
        left.removeFromTop (10);
        auto row = [&left] (int h = 30) { auto x = left.removeFromTop (h); left.removeFromTop (4); return x; };
        {
            auto b = row();
            const int w = b.getWidth() / 3;
            crank.setBounds (b.removeFromLeft (w).reduced (2, 0));
            instant.setBounds (b.removeFromLeft (w).reduced (2, 0));
            stop.setBounds (b.reduced (2, 0));
        }
        auto sliderRow = [&row] (juce::Label& l, juce::Slider& s)
        {
            auto b = row (28);
            l.setBounds (b.removeFromLeft (110));
            s.setBounds (b);
        };
        sliderRow (throttleL, throttle);
        sliderRow (loadL, load);
        {
            auto b = row (26);
            limiter.setBounds (b.removeFromLeft (b.getWidth() / 3));
            dyno.setBounds (b.removeFromLeft (b.getWidth() / 2));
            vehicle.setBounds (b);
        }
        {
            auto b = row (28);
            gearDown.setBounds (b.removeFromRight (70));
            b.removeFromRight (4);
            gearUp.setBounds (b.removeFromRight (70));
            brakeL.setBounds (b.removeFromLeft (110));
            brakeS.setBounds (b.withTrimmedRight (8));
        }
        sliderRow (dynoRpmL, dynoRpm);
        sliderRow (dynoLoadL, dynoLoad);
        scenarioL.setBounds (row (18));
        {
            auto b = row (60);
            const int w = b.getWidth() / 3;
            for (int i = 0; i < scenarioButtons.size(); ++i)
                scenarioButtons[i]->setBounds (b.getX() + (i % 3) * w, b.getY() + (i / 3) * 30, w - 4, 28);
        }
        modelNote.setBounds (left.removeFromBottom (36));
        history.setBounds (left);
        analyzer.setBounds (r);
    }

private:
    enum class Scenario { None, Crank, RevLimiter, LiftOff, Sweep, LoadStep, Traction, Hold };

    void runScenario (Scenario s)
    {
        scenario = s;
        scenarioTime = 0.0;
        auto& a = ctx.audio;
        if (s != Scenario::Sweep && s != Scenario::Hold) a.dynoMode.store (false);
        if (s != Scenario::Crank && ! a.ignition.load()) a.startEngineInstant();
        if (s == Scenario::Crank)
        {
            a.dynoMode.store (false);
            a.ignition.store (true);
            a.starter.store (true);
        }
    }

    void stepScenario (double dt)
    {
        if (scenario == Scenario::None) return;
        scenarioTime += dt;
        const double t = scenarioTime;
        auto& a = ctx.audio;
        const auto& e = ctx.doc.project().engine;
        switch (scenario)
        {
            case Scenario::Crank:
                if (a.isEngineRunning() && t > 0.4) { a.starter.store (false); scenario = Scenario::None; }
                if (t > 4.0) { a.starter.store (false); scenario = Scenario::None; }
                break;
            case Scenario::RevLimiter:
                a.externalLoad.store (0.0f);
                a.throttle.store (t < 3.5 ? 1.0f : 0.0f);
                if (t > 3.5) scenario = Scenario::None;
                break;
            case Scenario::LiftOff:
                a.externalLoad.store (t < 4.0 ? 0.6f : 0.0f);
                a.throttle.store (t < 2.5 ? 1.0f : 0.0f);
                if (t > 6.0) scenario = Scenario::None;
                break;
            case Scenario::Sweep:
            {
                a.dynoMode.store (true);
                a.dynoLoad.store (0.85f);
                const double dur = 8.0;
                a.dynoRpm.store (static_cast<float> (e.idleRpm + (e.redlineRpm - e.idleRpm) * juce::jlimit (0.0, 1.0, t / dur)));
                if (t > dur + 0.5) { a.dynoMode.store (false); a.throttle.store (0.0f); scenario = Scenario::None; }
                break;
            }
            case Scenario::LoadStep:
                a.throttle.store (0.55f);
                a.externalLoad.store (t > 1.5 && t < 4.5 ? 0.5f : 0.05f);
                if (t > 6.0) { a.throttle.store (0.0f); a.externalLoad.store (0.0f); scenario = Scenario::None; }
                break;
            case Scenario::Traction:
                // driven wheels lose grip: the load seen by the engine collapses, then grip returns
                a.throttle.store (t < 5.0 ? 1.0f : 0.0f);
                a.externalLoad.store (t < 1.5 ? 0.8f : t < 2.1 ? 0.05f : t < 5.0 ? 0.8f : 0.0f);
                if (t > 6.0) { a.externalLoad.store (0.0f); scenario = Scenario::None; }
                break;
            case Scenario::Hold:
                a.dynoMode.store (true);
                a.dynoRpm.store (3000.0f);
                a.dynoLoad.store (0.5f);
                scenario = Scenario::None;
                break;
            case Scenario::None: break;
        }
    }

    void timerCallback() override
    {
        stepScenario (1.0 / 30.0);
        const auto t = ctx.audio.telemetry();
        const auto& e = ctx.doc.project().engine;
        const auto kind = ctx.doc.project().kind;
        const bool vehicleView = ctx.audio.vehicleMode.load();
        if (kind == PowertrainKind::Electric)
        {
            readouts[0]->setTitle ("Motor speed", "rpm");
            readouts[1]->setTitle ("Motor torque", "Nm");
            readouts[2]->setTitle ("Power", "kW");
            readouts[3]->setTitle ("Torque / available", "%");
            readouts[5]->setTitle ("Phase current", "A");
            readouts[6]->setTitle ("Motor temp.", "C");
            readouts[7]->setTitle (vehicleView ? "Speed" : "DC voltage", vehicleView ? "km/h" : "V");
            readouts[8]->setTitle ("Electrical freq.", "Hz");
            readouts[9]->setTitle ("Gear", "");
            const double avail = ctx.doc.project().electric.torqueAt (t.motorRpm);
            const float frac = avail > 0 ? static_cast<float> (std::abs (t.motorTorque) / avail) : 0.0f;
            readouts[0]->set (t.motorRpm, 0);
            readouts[1]->set (t.motorTorque, 0, t.regen ? colours::good : juce::Colour());
            readouts[2]->set (t.motorPowerKw, 1);
            readouts[3]->set (frac * 100.0f, 0);
            readouts[4]->set (t.throttle * 100.0f, 0);
            readouts[5]->set (t.motorCurrent, 0);
            readouts[6]->set (t.motorTempC, 0);
            readouts[7]->set (vehicleView ? t.speedKmh : static_cast<float> (ctx.doc.project().electric.voltage), 0);
            readouts[8]->set (t.electricHz, 1);
            readouts[9]->setText (t.gear <= 0 ? juce::String ("N") : juce::String (t.gear));
            readouts[10]->setText (t.regen ? "REGENERATING" : std::abs (t.motorTorque) > 1.0f ? "MOTORING" : "COASTING", t.regen ? colours::good : colours::accent);
            readouts[11]->set (ctx.audio.cpuLoad() * 100.0f, 0);
            history.push (static_cast<float> (t.motorRpm / juce::jmax (1.0, ctx.doc.project().electric.maxRpm)), frac, t.throttle);
            syncControls (e);
            return;
        }
        readouts[0]->setTitle ("Engine speed", "rpm");
        readouts[1]->setTitle ("Brake torque", "Nm");
        readouts[2]->setTitle ("Power", "kW");
        readouts[3]->setTitle ("Load", "%");
        readouts[5]->setTitle ("Manifold", "bar");
        readouts[6]->setTitle ("Exhaust gas", "C");
        readouts[7]->setTitle (vehicleView ? "Speed" : "Coolant", vehicleView ? "km/h" : "C");
        readouts[8]->setTitle ("Firing freq.", "Hz");
        readouts[9]->setTitle (vehicleView ? "Gear" : "Turbo shaft", vehicleView ? "" : "%");
        readouts[0]->set (t.rpm, 0, t.limiter ? colours::bad : juce::Colour());
        readouts[1]->set (t.brakeTorque, 0);
        readouts[2]->set (t.powerKw, 1);
        readouts[3]->set (t.airflow * 100.0f, 0);
        readouts[4]->set (t.throttle * 100.0f, 0);
        readouts[5]->set (t.boostBar, 2, t.boostBar > 0.05f ? colours::accent2 : juce::Colour());
        readouts[6]->set (t.egtK - 273.15f, 0);
        readouts[7]->set (vehicleView ? t.speedKmh : t.coolantC, 0);
        readouts[8]->set (t.firingHz, 1);
        if (vehicleView)
            readouts[9]->setText (t.gear <= 0 ? juce::String ("N") : juce::String (t.gear) + (t.shifting ? "  shifting" : ""), t.shifting ? colours::warn : juce::Colour());
        else
            readouts[9]->set (t.turboShaft * 100.0f, 0);
        const juce::String state = ! t.combustion ? (t.starter ? "CRANKING" : (t.rpm > 30.0f ? "SPIN DOWN" : "OFF"))
                                   : t.limiter ? "LIMITER" : t.fuelCut ? "FUEL CUT" : "RUNNING";
        readouts[10]->setText (state, t.limiter ? colours::bad : t.combustion ? colours::good : colours::textDim);
        readouts[11]->set (ctx.audio.cpuLoad() * 100.0f, 0);
        history.push (static_cast<float> (t.rpm / juce::jmax (1.0, e.limiterRpm)), t.airflow, t.throttle);
        syncControls (e);
    }

    void syncControls (const ThermalEngineSpec& e)
    {
        // keep sliders in sync with keyboard / scenarios
        throttle.setValue (ctx.audio.throttle.load(), juce::dontSendNotification);
        load.setValue (ctx.audio.externalLoad.load(), juce::dontSendNotification);
        dyno.setToggleState (ctx.audio.dynoMode.load(), juce::dontSendNotification);
        dynoRpm.setValue (ctx.audio.dynoRpm.load(), juce::dontSendNotification);
        dynoLoad.setValue (ctx.audio.dynoLoad.load(), juce::dontSendNotification);
        vehicle.setToggleState (ctx.audio.vehicleMode.load(), juce::dontSendNotification);
        brakeS.setValue (ctx.audio.brake.load(), juce::dontSendNotification);
        const double maxRpm = ctx.doc.project().kind == PowertrainKind::Electric ? ctx.doc.project().electric.maxRpm : e.limiterRpm;
        dynoRpm.setRange (300.0, juce::jmax (1000.0, maxRpm), 10.0);
    }

    AnalyzerPanel analyzer;
    HistoryChart history;
    juce::OwnedArray<Readout> readouts { [] {
        juce::OwnedArray<Readout> r;
        r.add (new Readout ("Engine speed", "rpm"));
        r.add (new Readout ("Brake torque", "Nm"));
        r.add (new Readout ("Power", "kW"));
        r.add (new Readout ("Load", "%", 100.0f));
        r.add (new Readout ("Throttle", "%", 100.0f));
        r.add (new Readout ("Manifold", "bar"));
        r.add (new Readout ("Exhaust gas", "C"));
        r.add (new Readout ("Coolant", "C"));
        r.add (new Readout ("Firing freq.", "Hz"));
        r.add (new Readout ("Turbo shaft", "%", 100.0f));
        r.add (new Readout ("State", ""));
        r.add (new Readout ("Audio CPU", "%", 100.0f));
        return r;
    }() };
    juce::TextButton crank { "Crank (starter)" }, instant { "Instant start" }, stop { "Ignition off" };
    juce::ToggleButton limiter { "Rev limiter" }, dyno { "Dyno (hold rpm)" }, vehicle { "Vehicle (gearbox + car)" };
    juce::TextButton gearUp { "Gear +" }, gearDown { "Gear -" };
    juce::Slider throttle, load, dynoRpm, dynoLoad, brakeS;
    juce::Label throttleL, loadL, dynoRpmL, dynoLoadL, scenarioL, modelNote, brakeL;
    juce::OwnedArray<juce::TextButton> scenarioButtons;
    Scenario scenario = Scenario::None;
    double scenarioTime = 0.0;
};
} // namespace

std::unique_ptr<Page> createSimulatorPage (AppContext& c) { return std::make_unique<SimulatorPage> (c); }
} // namespace esd::ui
