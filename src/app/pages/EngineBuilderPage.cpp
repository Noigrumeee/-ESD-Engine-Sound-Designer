#include "Page.h"

#include "base/Math.h"
#include "widgets/CurveEditor.h"
#include "widgets/ParamEditors.h"
#include "widgets/Visualizers.h"

namespace esd::ui
{
namespace
{
/// Name, layout, crank and firing order (validated against the crank geometry).
class ArchitecturePanel : public juce::Component
{
public:
    explicit ArchitecturePanel (AppContext& c) : ctx (c)
    {
        for (auto* l : { &nameL, &layoutL, &crankL, &orderL, &customL })
        {
            l->setFont (uiFont (13.5f));
            addAndMakeVisible (l);
        }
        nameL.setText ("Name", juce::dontSendNotification);
        layoutL.setText ("Architecture", juce::dontSendNotification);
        crankL.setText ("Crankshaft", juce::dontSendNotification);
        orderL.setText ("Firing order", juce::dontSendNotification);
        customL.setText ("Custom order", juce::dontSendNotification);
        name.onReturnKey = name.onFocusLost = [this]
        {
            const auto t = name.getText().trim();
            if (t.isNotEmpty() && t.toStdString() != ctx.doc.project().name)
                ctx.doc.edit ([t] (Project& p) { p.name = t.toStdString(); }, "Rename", {}, true);
        };
        addAndMakeVisible (name);

        int id = 1;
        juce::String fam;
        for (auto& l : layoutLibrary())
        {
            if (fam != juce::String (l.family))
            {
                fam = l.family;
                layout.addSectionHeading (fam);
            }
            layout.addItem (juce::String (l.name) + "  (" + toString (l.cycle) + ")", id++);
        }
        layout.onChange = [this] { if (! updating) changeLayout(); };
        crank.onChange = [this] { if (! updating) changeCrank(); };
        order.onChange = [this] { if (! updating) changeOrder(); };
        custom.setTextToShowWhenEmpty ("e.g. 1-3-4-2", colours::textDim);
        custom.onReturnKey = [this] { applyCustom(); };
        apply.onClick = [this] { applyCustom(); };
        status.setFont (uiFont (12.5f));
        status.setJustificationType (juce::Justification::topLeft);
        for (auto* comp : std::initializer_list<juce::Component*> { &layout, &crank, &order, &custom, &apply, &status })
            addAndMakeVisible (comp);
        refresh();
    }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> svs (updating, true);
        const auto& e = ctx.doc.project().engine;
        if (! name.hasKeyboardFocus (true)) name.setText (ctx.doc.project().name, juce::dontSendNotification);
        const auto& lib = layoutLibrary();
        for (std::size_t i = 0; i < lib.size(); ++i)
            if (lib[i].id == e.layoutId) layout.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
        const auto* l = findLayout (e.layoutId);
        crank.clear (juce::dontSendNotification);
        order.clear (juce::dontSendNotification);
        if (! l) return;
        for (std::size_t i = 0; i < l->cranks.size(); ++i)
        {
            crank.addItem (l->cranks[i].name, static_cast<int> (i) + 1);
            if (l->cranks[i].id == e.crankId) crank.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
        }
        const auto* c = findCrank (*l, e.crankId);
        orders = possibleFiringOrders (*l, *c, e.bankAngleDeg);
        const auto currentPlan = computeFiringPlan (*l, *c, e.bankAngleDeg, e.firingOrder);
        for (std::size_t i = 0; i < orders.size(); ++i)
        {
            const auto p = computeFiringPlan (*l, *c, e.bankAngleDeg, orders[i]);
            juce::String txt = firingOrderToString (orders[i]);
            txt << (i == 0 && orders[i] == c->firingOrder ? "  (reference)" : "") << "   " << (p.evenFiring() ? "even" : "uneven");
            order.addItem (txt, static_cast<int> (i) + 1);
            if (currentPlan.valid && orders[i] == currentPlan.order) order.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
        }
        juce::String s;
        if (currentPlan.valid)
        {
            s << "Intervals:";
            for (double iv : currentPlan.intervals) s << " " << juce::String (juce::roundToInt (iv));
            s << " deg  (" << (currentPlan.evenFiring() ? "even firing" : "uneven firing") << ")";
            if (! c->note.empty()) s << "\n" << c->note;
            s << "\n" << juce::String (static_cast<int> (orders.size())) << " firing order(s) are mechanically possible with this crank (cam phasing).";
            status.setColour (juce::Label::textColourId, colours::textDim);
        }
        else
        {
            s = currentPlan.error;
            status.setColour (juce::Label::textColourId, colours::bad);
        }
        status.setText (s, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds(), "Architecture"); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8).withTrimmedTop (22);
        auto row = [&r] { return r.removeFromTop (30).reduced (0, 2); };
        auto split = [] (juce::Rectangle<int> rr, juce::Label& l) { l.setBounds (rr.removeFromLeft (110)); return rr; };
        name.setBounds (split (row(), nameL));
        layout.setBounds (split (row(), layoutL));
        crank.setBounds (split (row(), crankL));
        order.setBounds (split (row(), orderL));
        auto cr = split (row(), customL);
        apply.setBounds (cr.removeFromRight (64));
        custom.setBounds (cr.withTrimmedRight (6));
        status.setBounds (r.reduced (2, 2));
    }

private:
    void changeLayout()
    {
        const int idx = layout.getSelectedId() - 1;
        const auto& lib = layoutLibrary();
        if (idx < 0 || idx >= static_cast<int> (lib.size())) return;
        const auto& l = lib[static_cast<std::size_t> (idx)];
        const std::string id = l.id;
        ctx.doc.edit ([id] (Project& p)
        {
            const auto* nl = findLayout (id);
            p.engine.layoutId = id;
            p.engine.crankId = nl->cranks.front().id;
            p.engine.firingOrder.clear();
            p.engine.bankAngleDeg = 0.0;
            if (nl->cycle == CycleType::Wankel) p.engine.cam = CamType::PortRotary;
            else if (nl->cycle == CycleType::TwoStroke) p.engine.cam = CamType::PortTwoStroke;
            else if (p.engine.cam == CamType::PortRotary || p.engine.cam == CamType::PortTwoStroke) p.engine.cam = CamType::DOHC;
            // the cylinder count changed: the old torque curve no longer matches the displacement
            p.engine.torqueCurve = estimateTorqueCurve (p.engine, p.boost);
        }, "Change architecture");
        if (ctx.status) ctx.status ("Architecture changed: torque curve re-estimated from the new displacement (edit it in the curve editor).", false);
    }

    void changeCrank()
    {
        const auto* l = findLayout (ctx.doc.project().engine.layoutId);
        const int idx = crank.getSelectedId() - 1;
        if (! l || idx < 0 || idx >= static_cast<int> (l->cranks.size())) return;
        const std::string id = l->cranks[static_cast<std::size_t> (idx)].id;
        ctx.doc.edit ([id] (Project& p)
        {
            p.engine.crankId = id;
            p.engine.firingOrder.clear();
            p.engine.bankAngleDeg = 0.0;
        }, "Change crankshaft");
    }

    void changeOrder()
    {
        const int idx = order.getSelectedId() - 1;
        if (idx < 0 || idx >= static_cast<int> (orders.size())) return;
        const auto o = orders[static_cast<std::size_t> (idx)];
        ctx.doc.edit ([o] (Project& p) { p.engine.firingOrder = o; }, "Change firing order");
    }

    void applyCustom()
    {
        const auto o = parseFiringOrder (custom.getText().toStdString());
        const auto& e = ctx.doc.project().engine;
        const auto* l = findLayout (e.layoutId);
        const auto* c = findCrank (*l, e.crankId);
        const auto plan = computeFiringPlan (*l, *c, e.bankAngleDeg, o);
        if (! plan.valid)
        {
            status.setColour (juce::Label::textColourId, colours::bad);
            status.setText (o.empty() ? juce::String ("Syntax: cylinder numbers separated by '-'") : juce::String (plan.error), juce::dontSendNotification);
            return;
        }
        ctx.doc.edit ([o] (Project& p) { p.engine.firingOrder = o; }, "Custom firing order");
        custom.clear();
    }

    AppContext& ctx;
    juce::Label nameL, layoutL, crankL, orderL, customL, status;
    juce::TextEditor name, custom;
    juce::ComboBox layout, crank, order;
    juce::TextButton apply { "Apply" };
    std::vector<std::vector<int>> orders;
    bool updating = false;
};

class EngineBuilderPage : public Page
{
public:
    explicit EngineBuilderPage (AppContext& c) : Page (c), column (c), arch (c), live (c)
    {
        column.addCustom (arch, 260);
        column.addPanel ("Geometry", paramsInGroup ("engine.geometry"));
        column.addPanel ("Operating range & limiter", paramsInGroup ("engine.operation"));
        column.addPanel ("Valvetrain", paramsInGroup ("engine.valvetrain"));
        column.addPanel ("Combustion", paramsInGroup ("engine.combustion"));
        column.addPanel ("Intake", paramsInGroup ("intake"));
        column.addPanel ("Forced induction", paramsInGroup ("boost"));
        addAndMakeVisible (column);

        layoutMgr.setItemLayout (0, 360, 900, 470);
        layoutMgr.setItemLayout (1, 6, 6, 6);
        layoutMgr.setItemLayout (2, 400, -1.0, -0.6);
        addAndMakeVisible (resizer);

        addAndMakeVisible (infoText);
        infoText.setMultiLine (true);
        infoText.setReadOnly (true);
        infoText.setFont (uiFont (13.0f));
        infoText.setColour (juce::TextEditor::backgroundColourId, colours::panel);
        addAndMakeVisible (firing);
        addAndMakeVisible (curve);
        addAndMakeVisible (live);
        for (auto* b : { &torqueTab, &throttleTab, &estimate })
            addAndMakeVisible (b);
        torqueTab.setRadioGroupId (11);
        throttleTab.setRadioGroupId (11);
        torqueTab.setClickingTogglesState (true);
        throttleTab.setClickingTogglesState (true);
        torqueTab.setToggleState (true, juce::dontSendNotification);
        torqueTab.onClick = throttleTab.onClick = [this] { projectChanged(); };
        estimate.setTooltip ("Replace the torque curve by an estimate from displacement, compression and induction (BMEP model)");
        estimate.onClick = [this]
        {
            ctx.doc.edit ([] (Project& p) { p.engine.torqueCurve = estimateTorqueCurve (p.engine, p.boost); }, "Estimate torque curve");
        };
        curve.onEdit = [this] (const Curve& cv) { pushCurve (cv); };
        curve.onEditEnd = [this] (const Curve& cv) { pushCurve (cv); };
        projectChanged();
    }

    void projectChanged() override
    {
        arch.refresh();
        column.refresh();
        const auto& p = ctx.doc.project();
        const auto& e = p.engine;
        const auto* l = findLayout (e.layoutId);
        const auto* c = findCrank (*l, e.crankId);
        const auto plan = computeFiringPlan (*l, *c, e.bankAngleDeg, e.firingOrder);
        firing.setPlan (plan, l->banks);

        const bool torque = torqueTab.getToggleState();
        if (torque)
        {
            curve.setCurve (e.torqueCurve);
            const double xMax = e.limiterRpm * 1.05;
            curve.setRanges (0.0, xMax, 0.0, juce::jmax (50.0, e.torqueCurve.maxY() * 1.3));
            curve.setAxisLabels ("rpm", "Brake torque at WOT (Nm)", "Power (kW)");
            const Curve tc = e.torqueCurve;
            curve.overlay = [tc] (double x) { return tc.eval (x) * x * kTwoPi / 60.0 / 1000.0; };
            double pMax = 1.0;
            for (int i = 0; i <= 200; ++i) pMax = juce::jmax (pMax, curve.overlay (xMax * i / 200.0));
            curve.overlayMax = pMax * 1.2;
            curve.setMarkers ({ { e.idleRpm, "idle", colours::textDim }, { e.redlineRpm, "redline", colours::warn }, { e.limiterRpm, "limiter", colours::bad } });
        }
        else
        {
            curve.setCurve (e.throttleMap);
            curve.setRanges (0.0, 1.0, 0.0, 1.0);
            curve.setAxisLabels ("pedal position", "airflow (cylinder filling) fraction");
            curve.overlay = nullptr;
            curve.setMarkers ({});
        }
        estimate.setVisible (torque);
        updateInfo (plan);
        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        juce::Component* comps[] = { &column, &resizer, &right };
        right.setBounds (0, 0, 0, 0);
        layoutMgr.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), false, true);
        auto rr = right.getBounds();
        live.setBounds (rr.removeFromBottom (48));
        rr.removeFromBottom (8);
        auto top = rr.removeFromTop (rr.getHeight() * 45 / 100);
        firing.setBounds (top.removeFromRight (juce::jmin (top.getWidth() / 2, 380)));
        top.removeFromRight (8);
        infoText.setBounds (top);
        rr.removeFromTop (8);
        auto tabs = rr.removeFromTop (28);
        torqueTab.setBounds (tabs.removeFromLeft (130));
        tabs.removeFromLeft (4);
        throttleTab.setBounds (tabs.removeFromLeft (130));
        estimate.setBounds (tabs.removeFromRight (220));
        rr.removeFromTop (4);
        curve.setBounds (rr);
    }

private:
    void pushCurve (const Curve& cv)
    {
        if (torqueTab.getToggleState())
            ctx.doc.edit ([cv] (Project& p) { p.engine.torqueCurve = cv; }, "Edit torque curve", "torqueCurve");
        else
            ctx.doc.edit ([cv] (Project& p) { p.engine.throttleMap = cv; }, "Edit throttle map", "throttleMap");
    }

    void updateInfo (const FiringPlan& plan)
    {
        const auto& p = ctx.doc.project();
        const auto& e = p.engine;
        const auto* l = findLayout (e.layoutId);
        const auto* c = findCrank (*l, e.crankId);
        EngineSim sim;
        sim.configure (p);
        double peakT = 0, peakTRpm = 0, peakP = 0, peakPRpm = 0;
        for (double rpm = e.idleRpm; rpm <= e.limiterRpm; rpm += 25.0)
        {
            const double t = e.torqueCurve.eval (rpm);
            const double pw = t * rpm * kTwoPi / 60.0 / 1000.0;
            if (t > peakT) { peakT = t; peakTRpm = rpm; }
            if (pw > peakP) { peakP = pw; peakPRpm = rpm; }
        }
        const double eventsPerRev = plan.valid ? plan.events.size() * 360.0 / plan.cycleDeg : 0.0;
        juce::String s;
        s << "COMPUTED FROM THE MECHANICAL MODEL\n";
        s << "Displacement  " << juce::String (juce::roundToInt (e.displacementCc())) << " cc   (" << juce::String (juce::roundToInt (e.cylinderDisplacementCc())) << " cc / "
          << (l->cycle == CycleType::Wankel ? "rotor" : "cylinder") << ")\n";
        s << "Bore / stroke  " << juce::String (e.boreStrokeRatio(), 2) << (e.boreStrokeRatio() > 1.05 ? "  (oversquare)" : e.boreStrokeRatio() < 0.95 ? "  (undersquare)" : "  (square)") << "\n";
        s << "Cycle  " << toString (l->cycle) << ", " << juce::String (eventsPerRev, 2) << " combustion events per crank revolution\n";
        s << "Firing frequency  " << juce::String (e.idleRpm / 60.0 * eventsPerRev, 1) << " Hz at idle,  " << juce::String (e.redlineRpm / 60.0 * eventsPerRev, 1)
          << " Hz at redline  (engine order " << juce::String (eventsPerRev, 2) << ")\n";
        s << "Mean piston speed at redline  " << juce::String (e.meanPistonSpeed (e.redlineRpm), 1) << " m/s\n";
        s << "Peak torque  " << juce::String (juce::roundToInt (peakT)) << " Nm @ " << juce::String (juce::roundToInt (peakTRpm)) << " rpm\n";
        s << "Peak power  " << juce::String (juce::roundToInt (peakP)) << " kW (" << juce::String (juce::roundToInt (peakP * 1.341)) << " hp) @ " << juce::String (juce::roundToInt (peakPRpm)) << " rpm\n";
        s << "Rotating inertia  " << juce::String (sim.inertia(), 3) << " kg m2" << (e.flywheelInertia > 0 ? "" : " (estimated from displacement)") << "\n";
        s << "Friction at redline  " << juce::String (juce::roundToInt (sim.frictionTorqueAt (e.redlineRpm))) << " Nm (FMEP model)\n";
        s << "Balance (table approximation)  primary " << juce::String (c->balance.primary, 2) << ", secondary " << juce::String (c->balance.secondary, 2)
          << ", rocking couple " << juce::String (c->balance.couple, 2) << "\n";
        auto issues = validate (p);
        if (! issues.empty())
        {
            s << "\nCHECKS\n";
            for (auto& i : issues) s << (i.severity == Severity::Error ? "[error] " : i.severity == Severity::Warning ? "[warning] " : "[info] ") << i.message << "\n";
        }
        infoText.setText (s, juce::dontSendNotification);
    }

    ParamColumn column;
    ArchitecturePanel arch;
    LiveControls live;
    juce::StretchableLayoutManager layoutMgr;
    juce::StretchableLayoutResizerBar resizer { &layoutMgr, 1, true };
    juce::Component right;
    juce::TextEditor infoText;
    FiringDiagram firing;
    CurveEditor curve;
    juce::TextButton torqueTab { "Torque curve" }, throttleTab { "Throttle map" }, estimate { "Re-estimate from displacement" };
};
} // namespace

std::unique_ptr<Page> createEngineBuilderPage (AppContext& c) { return std::make_unique<EngineBuilderPage> (c); }
} // namespace esd::ui
