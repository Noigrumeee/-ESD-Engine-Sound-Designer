#include "CurveEditor.h"
#include "Theme.h"

namespace esd::ui
{
CurveEditor::CurveEditor() { setRepaintsOnMouseActivity (false); }

void CurveEditor::setCurve (const Curve& cv)
{
    if (dragIndex >= 0) return; // do not fight the user while dragging
    c = cv;
    repaint();
}

void CurveEditor::setRanges (double x0, double x1, double y0, double y1)
{
    xa = x0; xb = juce::jmax (x0 + 1e-6, x1);
    ya = y0; yb = juce::jmax (y0 + 1e-6, y1);
    repaint();
}

void CurveEditor::setAxisLabels (juce::String x, juce::String y, juce::String o)
{
    xLabel = std::move (x);
    yLabel = std::move (y);
    oLabel = std::move (o);
}

juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (52).withTrimmedRight (overlay ? 52.0f : 12.0f).withTrimmedTop (12).withTrimmedBottom (30);
}

juce::Point<float> CurveEditor::toScreen (double x, double y) const
{
    auto a = plotArea();
    return { a.getX() + static_cast<float> ((x - xa) / (xb - xa)) * a.getWidth(), a.getBottom() - static_cast<float> ((y - ya) / (yb - ya)) * a.getHeight() };
}

std::pair<double, double> CurveEditor::fromScreen (juce::Point<float> p) const
{
    auto a = plotArea();
    return { xa + (p.x - a.getX()) / a.getWidth() * (xb - xa), ya + (a.getBottom() - p.y) / a.getHeight() * (yb - ya) };
}

int CurveEditor::hitPoint (juce::Point<float> p) const
{
    for (std::size_t i = 0; i < c.points().size(); ++i)
        if (toScreen (c.points()[i].x, c.points()[i].y).getDistanceFrom (p) < 8.0f) return static_cast<int> (i);
    return -1;
}

static juce::String niceNumber (double v)
{
    if (std::abs (v) >= 1000.0) return juce::String (v / 1000.0, 1) + "k";
    if (std::abs (v) >= 10.0) return juce::String (juce::roundToInt (v));
    return juce::String (v, 2);
}

void CurveEditor::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds(), {});
    auto a = plotArea();
    g.setFont (uiFont (11.0f));

    // grid
    for (int i = 0; i <= 5; ++i)
    {
        const float y = a.getBottom() - a.getHeight() * static_cast<float> (i) / 5.0f;
        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (colours::textDim);
        g.drawText (niceNumber (ya + (yb - ya) * i / 5.0), juce::Rectangle<float> (0, y - 7, 48, 14), juce::Justification::centredRight);
        if (overlay)
            g.drawText (niceNumber (overlayMax * i / 5.0), juce::Rectangle<float> (a.getRight() + 4, y - 7, 46, 14), juce::Justification::centredLeft);
    }
    for (int i = 0; i <= 8; ++i)
    {
        const float x = a.getX() + a.getWidth() * static_cast<float> (i) / 8.0f;
        g.setColour (colours::outline.withAlpha (0.4f));
        g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
        g.setColour (colours::textDim);
        g.drawText (niceNumber (xa + (xb - xa) * i / 8.0), juce::Rectangle<float> (x - 24, a.getBottom() + 2, 48, 14), juce::Justification::centred);
    }
    g.drawText (xLabel, a.withTop (a.getBottom() + 14).withHeight (14), juce::Justification::centred);
    g.setColour (colours::accent);
    g.drawText (yLabel, juce::Rectangle<float> (a.getX() + 4, a.getY(), 200, 14), juce::Justification::centredLeft);
    if (overlay)
    {
        g.setColour (colours::accent2);
        g.drawText (oLabel, juce::Rectangle<float> (a.getRight() - 204, a.getY(), 200, 14), juce::Justification::centredRight);
    }

    // markers (labels staggered so close markers stay readable)
    int markerIndex = 0;
    for (auto& m : markers)
    {
        const float labelY = a.getY() + 14.0f + 14.0f * static_cast<float> (markerIndex++ % 3);
        if (m.x < xa || m.x > xb) continue;
        const float x = toScreen (m.x, ya).x;
        g.setColour (m.colour.withAlpha (0.7f));
        const float dash[] = { 4.0f, 3.0f };
        g.drawDashedLine ({ x, a.getY(), x, a.getBottom() }, dash, 2, 1.0f);
        g.drawText (m.label, juce::Rectangle<float> (x + 3, labelY, 120, 14), juce::Justification::centredLeft);
    }

    // overlay curve
    if (overlay && overlayMax > 0)
    {
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const double x = xa + (xb - xa) * i / 200.0;
            const double v = overlay (x) / overlayMax;
            const auto s = juce::Point<float> (toScreen (x, ya).x, a.getBottom() - static_cast<float> (v) * a.getHeight());
            i == 0 ? p.startNewSubPath (s) : p.lineTo (s);
        }
        g.setColour (colours::accent2);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    // main curve
    if (! c.empty())
    {
        juce::Path p;
        for (int i = 0; i <= 300; ++i)
        {
            const double x = xa + (xb - xa) * i / 300.0;
            const auto s = toScreen (x, c.eval (x));
            i == 0 ? p.startNewSubPath (s) : p.lineTo (s);
        }
        g.setColour (colours::accent);
        g.strokePath (p, juce::PathStrokeType (2.2f));
        for (std::size_t i = 0; editable && i < c.points().size(); ++i)
        {
            const auto s = toScreen (c.points()[i].x, c.points()[i].y);
            const bool hot = static_cast<int> (i) == hoverIndex || static_cast<int> (i) == dragIndex;
            g.setColour (hot ? colours::text : colours::accent);
            g.fillEllipse (s.x - 4.5f, s.y - 4.5f, 9.0f, 9.0f);
            if (hot)
            {
                g.setColour (colours::text);
                g.drawText (niceNumber (c.points()[i].x) + " / " + niceNumber (c.points()[i].y), juce::Rectangle<float> (s.x + 8, s.y - 18, 120, 14),
                            juce::Justification::centredLeft);
            }
        }
    }
    if (! editable) return;
    g.setColour (colours::textDim.withAlpha (0.7f));
    g.drawText ("drag points - double-click: add - right-click: delete", getLocalBounds().toFloat().reduced (8, 2).removeFromBottom (12),
                juce::Justification::centredRight);
}

void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    if (! editable) return;
    const int h = hitPoint (e.position);
    if (h != hoverIndex)
    {
        hoverIndex = h;
        repaint();
    }
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! editable) return;
    const int h = hitPoint (e.position);
    if (e.mods.isPopupMenu())
    {
        if (h >= 0 && c.points().size() > 2)
        {
            c.removePoint (static_cast<std::size_t> (h));
            hoverIndex = -1;
            if (onEditEnd) onEditEnd (c);
            repaint();
        }
        return;
    }
    dragIndex = h;
    edited = false;
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! editable) return;
    if (dragIndex < 0) return;
    auto [x, y] = fromScreen (e.position);
    x = juce::jlimit (xa, xb, x);
    y = juce::jlimit (ya, yb, y);
    c.movePoint (static_cast<std::size_t> (dragIndex), { std::round (x), y });
    edited = true;
    if (onEdit) onEdit (c);
    repaint();
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex >= 0 && edited && onEditEnd) onEditEnd (c);
    dragIndex = -1;
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable) return;
    if (hitPoint (e.position) >= 0) return;
    auto [x, y] = fromScreen (e.position);
    if (x < xa || x > xb) return;
    c.addPoint ({ std::round (x), juce::jlimit (ya, yb, y) });
    if (onEditEnd) onEditEnd (c);
    repaint();
}
} // namespace esd::ui
