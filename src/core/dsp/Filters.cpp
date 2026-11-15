#include "Filters.h"

#include <complex>

namespace esd::dsp
{
void Biquad::design (int type, double fs, double f, double q, double gainDb)
{
    f = clampv (f, 5.0, fs * 0.49);
    q = std::max (q, 0.05);
    const double w0 = kTwoPi * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
    switch (type)
    {
        case 0: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case 1: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case 2: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case 3: B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        case 4:
            B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
            A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A;
            break;
        case 5:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
            A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
            break;
        }
        case 6:
        {
            const double s = 2 * std::sqrt (A) * alpha;
            B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
            A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
            break;
        }
        default: break;
    }
    b0 = static_cast<float> (B0 / A0);
    b1 = static_cast<float> (B1 / A0);
    b2 = static_cast<float> (B2 / A0);
    a1 = static_cast<float> (A1 / A0);
    a2 = static_cast<float> (A2 / A0);
}

double Biquad::magnitude (double fs, double f) const
{
    const std::complex<double> z1c = std::polar (1.0, -kTwoPi * f / fs);
    const std::complex<double> z2c = z1c * z1c;
    const auto num = static_cast<double> (b0) + static_cast<double> (b1) * z1c + static_cast<double> (b2) * z2c;
    const auto den = 1.0 + static_cast<double> (a1) * z1c + static_cast<double> (a2) * z2c;
    return std::abs (num / den);
}
} // namespace esd::dsp
