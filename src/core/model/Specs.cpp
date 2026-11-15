#include "Specs.h"
#include "../base/Math.h"

namespace esd
{
double ElectricSpec::baseRpm() const
{
    if (peakTorqueNm <= 0.0) return maxRpm;
    const double w = peakPowerKw * 1000.0 / peakTorqueNm;
    return clampv (radPerSecToRpm (w), 100.0, maxRpm);
}

double ElectricSpec::torqueAt (double rpm) const
{
    rpm = std::abs (rpm);
    if (rpm >= maxRpm) return 0.0;
    double t = peakTorqueNm;
    const double base = baseRpm();
    if (rpm > base) t = peakPowerKw * 1000.0 / rpmToRadPerSec (rpm); // field weakening: constant power
    const double taper = clampv ((maxRpm - rpm) / (0.03 * maxRpm), 0.0, 1.0);
    return t * taper;
}

const char* layerName (LayerId id)
{
    switch (id)
    {
        case LayerId::Combustion: return "Combustion";
        case LayerId::Intake: return "Intake";
        case LayerId::Exhaust: return "Exhaust";
        case LayerId::Mechanical: return "Mechanical";
        case LayerId::Boost: return "Boost";
        case LayerId::Vibration: return "Vibration";
        case LayerId::Electromagnetic: return "Electromagnetic";
        case LayerId::Whine: return "Whine";
        case LayerId::Rotor: return "Rotor";
        case LayerId::Bearings: return "Bearings";
        case LayerId::Inverter: return "Inverter";
        case LayerId::Cooling: return "Cooling";
        case LayerId::Resonance: return "Resonance";
        case LayerId::Transmission: return "Transmission";
        case LayerId::Samples: return "Samples";
        case LayerId::Count: break;
    }
    return "?";
}

bool layerUsedBy (LayerId id, int kind)
{
    const int i = static_cast<int> (id);
    if (id == LayerId::Transmission || id == LayerId::Samples || id == LayerId::Vibration) return true;
    if (kind == 0) return i <= static_cast<int> (LayerId::Vibration);
    if (kind == 1) return i >= static_cast<int> (LayerId::Electromagnetic) && i <= static_cast<int> (LayerId::Resonance);
    return true;
}

const char* toString (ModSource s)
{
    switch (s)
    {
        case ModSource::Rpm: return "rpm";
        case ModSource::Torque: return "torque";
        case ModSource::Load: return "load";
        case ModSource::Throttle: return "throttle";
        case ModSource::Speed: return "speed";
        case ModSource::Gear: return "gear";
        case ModSource::Temperature: return "temperature";
        case ModSource::Boost: return "boost";
        case ModSource::ElectricFreq: return "electric_freq";
        case ModSource::Battery: return "battery";
        case ModSource::Regen: return "regen";
        case ModSource::ShiftEvent: return "shift_event";
        case ModSource::Count: break;
    }
    return "rpm";
}

const char* toString (ModTarget t)
{
    switch (t)
    {
        case ModTarget::GainDb: return "gain_db";
        case ModTarget::Pan: return "pan";
        case ModTarget::LowpassHz: return "lowpass";
        case ModTarget::Drive: return "drive";
        case ModTarget::Count: break;
    }
    return "gain_db";
}
} // namespace esd
