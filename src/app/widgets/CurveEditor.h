#pragma once
// Interactive curve editor: drag points, double-click to add, right-click to delete.
#include <JuceHeader.h>

#include "base/Curve.h"

namespace esd::ui
{
class CurveEditor : public juce::Component
{
public:
    struct Marker
    {
        double x;
        juce::String label;
        juce::Colour colour;
    };

    CurveEditor();

    void setCurve (const Curve&);
    const Curve& curve() const { return c; }
    void setRanges (double x0, double x1, double y0, double y1);
    void setAxisLabels (juce::String x, juce::String y, juce::String overlayY = {});
    void setMarkers (std::vector<Marker> m) { markers = std::move (m); repaint(); }
    /// Optional second curve drawn on its own right-hand scale (e.g. power from torque).
    std::function<double (double x)> overlay;
    double overlayMax = 1.0;
    juce::String overlayUnit;

    bool editable = true;                          ///< false: display only (computed curves)
    std::function<void (const Curve&)> onEdit;      ///< while dragging
    std::function<void (const Curve&)> onEditEnd;   ///< mouse up

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (double x, double y) const;
    std::pair<double, double> fromScreen (juce::Point<float>) const;
    int hitPoint (juce::Point<float>) const;

    Curve c;
    double xa = 0, xb = 1, ya = 0, yb = 1;
    juce::String xLabel, yLabel, oLabel;
    std::vector<Marker> markers;
    int dragIndex = -1, hoverIndex = -1;
    bool edited = false;
};
} // namespace esd::ui
