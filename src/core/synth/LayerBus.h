#pragma once
// The layer bus: every sound source (thermal voice, electric voice, transmission, samples)
// writes its layers here; the LayerMixer then applies per-layer FX, automation, gain / pan,
// the shared reverb and the master stage.
#include "../dsp/Effects.h"
#include "../model/Project.h"

#include <array>
#include <functional>
#include <vector>

namespace esd
{
struct LayerBus
{
    float* L[kNumLayers] {};
    float* R[kNumLayers] {};
};

/// Live mixer values (gain includes mute / solo). Changing them never rebuilds anything.
struct VoiceMix
{
    std::array<float, kNumLayers> gain {};
    std::array<float, kNumLayers> pan {};
    float master = 0.5f;
    float width = 1.0f;
};
VoiceMix mixFromProject (const SoundMix&);

/// Normalised (0..1) automation sources for the current block.
struct ModInputs
{
    std::array<float, static_cast<int> (ModSource::Count)> v {};
    float& operator[] (ModSource s) { return v[static_cast<std::size_t> (s)]; }
    float operator[] (ModSource s) const { return v[static_cast<std::size_t> (s)]; }
};

/// Loads a mono impulse response at a given sample rate (provided by the app or the CLI).
using IrLoader = std::function<std::vector<float> (const std::string& path, double sampleRate)>;

class LayerMixer
{
public:
    void prepare (const SoundMix&, double fs, const IrLoader& irLoader = {});
    /// Mixes bus[0..n) to out. layerOutL/R (optional) receive each layer after FX and gain.
    void process (const LayerBus& bus, const VoiceMix&, const ModInputs&, float* outL, float* outR, int n,
                  float* const* layerOutL = nullptr, float* const* layerOutR = nullptr);
    const std::array<float, kNumLayers>& layerRms() const { return rms; }

private:
    struct Chain
    {
        dsp::Biquad hpL, hpR, lowL, lowR, midL, midR, highL, highR, lpL, lpR, autoLpL, autoLpR;
        bool hp = false, low = false, mid = false, high = false, lp = false;
        dsp::Saturator sat;
        float baseDrive = 0.0f;
        dsp::Compressor comp;
        dsp::Convolver conv;
        float convMix = 0.0f;
        float send = 0.0f;
        dsp::Smoother gainSm, panSm, lpSm;
        float autoLpNorm = 1.0f;
    };
    double fs = 48000.0;
    std::array<Chain, kNumLayers> chains;
    std::vector<Automation> automations;
    std::array<bool, kNumLayers> active {};
    dsp::Reverb reverb;
    float reverbReturn = 0.5f;
    bool reverbUsed = false;
    dsp::DcBlocker dcL, dcR;
    std::array<float, kNumLayers> rms {};
};
} // namespace esd
