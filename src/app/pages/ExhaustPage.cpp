#include "Page.h"

#include "widgets/ParamEditors.h"
#include "widgets/Visualizers.h"

namespace esd::ui
{
namespace
{
class ExhaustPage : public Page
{
public:
    explicit ExhaustPage (AppContext& c) : Page (c), column (c), analyzer (c, AnalyzerPanel::Spectrum), live (c)
    {
        column.addPanel ("Headers", paramsInGroup ("exhaust.header"));
        column.addPanel ("Line & routing", paramsInGroup ("exhaust.line"));
        column.addPanel ("Silencing", paramsInGroup ("exhaust.silencing"));
        column.addPanel ("Outlets", paramsInGroup ("exhaust.outlet"));
        addAndMakeVisible (column);
        addAndMakeVisible (diagram);
        addAndMakeVisible (analyzer);
        addAndMakeVisible (live);
        layoutMgr.setItemLayout (0, 340, 800, 430);
        layoutMgr.setItemLayout (1, 6, 6, 6);
        layoutMgr.setItemLayout (2, 400, -1.0, -0.6);
        addAndMakeVisible (resizer);
        projectChanged();
    }

    void projectChanged() override
    {
        column.refresh();
        diagram.setProject (ctx.doc.project());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        juce::Component* comps[] = { &column, &resizer, &right };
        layoutMgr.layOutComponents (comps, 3, r.getX(), r.getY(), r.getWidth(), r.getHeight(), false, true);
        auto rr = right.getBounds();
        live.setBounds (rr.removeFromBottom (48));
        rr.removeFromBottom (8);
        diagram.setBounds (rr.removeFromTop (rr.getHeight() * 48 / 100));
        rr.removeFromTop (8);
        analyzer.setBounds (rr);
    }

private:
    ParamColumn column;
    ExhaustDiagram diagram;
    AnalyzerPanel analyzer;
    LiveControls live;
    juce::StretchableLayoutManager layoutMgr;
    juce::StretchableLayoutResizerBar resizer { &layoutMgr, 1, true };
    juce::Component right;
};
} // namespace

std::unique_ptr<Page> createExhaustPage (AppContext& c) { return std::make_unique<ExhaustPage> (c); }
} // namespace esd::ui
