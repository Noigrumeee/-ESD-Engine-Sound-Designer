#include "LayerBus.h"
#include "../base/Math.h"

#include <cmath>

namespace esd
{
VoiceMix mixFromProject (const SoundMix& m)
{
    VoiceMix v;
    bool anySolo = false;
    for (auto& l : m.layers) anySolo = anySolo || l.solo;
    for (int i = 0; i < kNumLayers; ++i)
    {
        const auto& l = m.layers[static_cast<std::size_t> (i)];
        const bool audible = ! l.mute && (! anySolo || l.solo);
        v.gain[static_cast<std::size_t> (i)] = audible ? dbToGain (l.gainDb) : 0.0f;
        v.pan[static_cast<std::size_t> (i)] = l.pan;
    }
    v.master = dbToGain (m.masterGainDb);
    v.width = m.stereoWidth;
    return v;
}

namespace
{
inline void equalPowerBalance (float pan, float& l, float& r)
{
    const float th = (clampv (pan, -1.0f, 1.0f) + 1.0f) * static_cast<float> (kPi) * 0.25f;
    l = std::cos (th) * 1.41421356f;
    r = std::sin (th) * 1.41421356f;
}

float lpFromNorm (float x) { return 100.0f * std::pow (200.0f, clampv (x, 0.0f, 1.0f)); } // 100 Hz .. 20 kHz
} // namespace

void LayerMixer::prepare (const SoundMix& m, double sampleRate, const IrLoader& irLoader)
{
    fs = sampleRate;
    automations = m.automations;
    reverbUsed = false;
    for (int i = 0; i < kNumLayers; ++i)
    {
        auto& c = chains[static_cast<std::size_t> (i)];
        const auto& f = m.fx[static_cast<std::size_t> (i)];
        c = Chain {};
        c.hp = f.highpassHz > 1.0f;
        if (c.hp) { c.hpL.setHighpass (fs, f.highpassHz, 0.707); c.hpR = c.hpL; }
        c.low = std::abs (f.lowDb) > 0.05f;
        if (c.low) { c.lowL.setLowShelf (fs, 200.0, f.lowDb); c.lowR = c.lowL; }
        c.mid = std::abs (f.midDb) > 0.05f;
        if (c.mid) { c.midL.setPeak (fs, clampv (f.midHz, 50.0f, 15000.0f), 1.0, f.midDb); c.midR = c.midL; }
        c.high = std::abs (f.highDb) > 0.05f;
        if (c.high) { c.highL.setHighShelf (fs, 4000.0, f.highDb); c.highR = c.highL; }
        c.lp = f.lowpassHz > 1.0f;
        if (c.lp) { c.lpL.setLowpass (fs, f.lowpassHz, 0.707); c.lpR = c.lpL; }
        c.baseDrive = f.drive;
        c.comp.prepare (fs);
        c.comp.set (f.compThresholdDb, f.compRatio);
        c.convMix = f.convMix;
        if (! f.irPath.empty() && f.convMix > 0.0f && irLoader)
        {
            const auto ir = irLoader (f.irPath, fs);
            if (! ir.empty()) c.conv.prepare (ir, fs);
        }
        c.send = f.reverbSend;
        reverbUsed = reverbUsed || c.send > 0.0f;
        c.gainSm.setTime (fs, 0.02);
        c.panSm.setTime (fs, 0.03);
        c.lpSm.setTime (fs, 0.03);
        c.gainSm.snap (1.0f);
        c.lpSm.snap (1.0f);
        c.autoLpL.setIdentity();
        c.autoLpR.setIdentity();
    }
    reverb.prepare (fs);
    reverb.set (m.reverbSize, m.reverbDamping);
    reverbReturn = dbToGain (m.reverbReturnDb);
    dcL.setCutoff (fs, 12.0);
    dcR.setCutoff (fs, 12.0);
}

void LayerMixer::process (const LayerBus& bus, const VoiceMix& mix, const ModInputs& mod, float* outL, float* outR, int n,
                          float* const* layerOutL, float* const* layerOutR)
{
    // ---- automation (block rate, smoothed per sample)
    std::array<float, kNumLayers> gainDb {}, pan {}, drive {};
    std::array<float, kNumLayers> lpNorm;
    lpNorm.fill (1.0f);
    std::array<bool, kNumLayers> hasLp {};
    for (const auto& a : automations)
    {
        if (! a.enabled || a.layer < 0 || a.layer >= kNumLayers) continue;
        const auto li = static_cast<std::size_t> (a.layer);
        const float y = static_cast<float> (a.curve.eval (clampv (mod[a.source], 0.0f, 1.0f)));
        switch (a.target)
        {
            case ModTarget::GainDb: gainDb[li] += y; break;
            case ModTarget::Pan: pan[li] += y; break;
            case ModTarget::LowpassHz: lpNorm[li] = std::min (lpNorm[li], y); hasLp[li] = true; break;
            case ModTarget::Drive: drive[li] = std::max (drive[li], y); break;
            case ModTarget::Count: break;
        }
    }

    for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
    std::array<double, kNumLayers> sq {};

    for (int l = 0; l < kNumLayers; ++l)
    {
        const auto li = static_cast<std::size_t> (l);
        if (! bus.L[l] || ! bus.R[l]) continue;
        auto& c = chains[li];
        const float gTarget = mix.gain[li] * dbToGain (clampv (gainDb[li], -80.0f, 24.0f));
        const float pTarget = clampv (mix.pan[li] + pan[li], -1.0f, 1.0f);
        c.sat.drive = clampv (c.baseDrive + drive[li], 0.0f, 1.0f);
        const bool autoLp = hasLp[li];
        if (autoLp)
        {
            const float lpN = c.lpSm.next (lpNorm[li]); // block-rate smoothing of the cutoff
            c.autoLpL.setLowpass (fs, lpFromNorm (lpN), 0.707);
            c.autoLpR.b0 = c.autoLpL.b0; c.autoLpR.b1 = c.autoLpL.b1; c.autoLpR.b2 = c.autoLpL.b2;
            c.autoLpR.a1 = c.autoLpL.a1; c.autoLpR.a2 = c.autoLpL.a2;
        }
        const bool silentNow = gTarget == 0.0f && c.gainSm.value < 1e-5f;
        float* bl = bus.L[l];
        float* br = bus.R[l];
        for (int i = 0; i < n; ++i)
        {
            float x = bl[i], y = br[i];
            if (silentNow)
            {
                if (layerOutL && layerOutL[l]) layerOutL[l][i] = 0.0f;
                if (layerOutR && layerOutR[l]) layerOutR[l][i] = 0.0f;
                continue;
            }
            if (c.hp) { x = c.hpL.process (x); y = c.hpR.process (y); }
            if (c.low) { x = c.lowL.process (x); y = c.lowR.process (y); }
            if (c.mid) { x = c.midL.process (x); y = c.midR.process (y); }
            if (c.high) { x = c.highL.process (x); y = c.highR.process (y); }
            if (c.lp) { x = c.lpL.process (x); y = c.lpR.process (y); }
            if (autoLp) { x = c.autoLpL.process (x); y = c.autoLpR.process (y); }
            x = c.sat.process (x);
            y = c.sat.process (y);
            c.comp.process (x, y);
            if (c.conv.ready())
            {
                float wx = x, wy = y;
                c.conv.process (wx, wy);
                x = x * (1.0f - c.convMix) + wx * c.convMix;
                y = y * (1.0f - c.convMix) + wy * c.convMix;
            }
            const float g = c.gainSm.next (gTarget);
            float pl, pr;
            equalPowerBalance (c.panSm.next (pTarget), pl, pr);
            x *= g * pl;
            y *= g * pr;
            sq[li] += 0.5 * (static_cast<double> (x) * x + static_cast<double> (y) * y);
            if (layerOutL && layerOutL[l]) layerOutL[l][i] = x;
            if (layerOutR && layerOutR[l]) layerOutR[l][i] = y;
            outL[i] += x;
            outR[i] += y;
            if (c.send > 0.0f)
            {
                // reverb input accumulates in the bus buffers (reused as scratch after reading)
                bl[i] = x * c.send;
                br[i] = y * c.send;
            }
            else
            {
                bl[i] = 0.0f;
                br[i] = 0.0f;
            }
        }
        rms[li] = static_cast<float> (std::sqrt (sq[li] / std::max (1, n)));
    }

    // ---- shared reverb: the bus buffers now hold the per-layer sends
    if (reverbUsed)
        for (int i = 0; i < n; ++i)
        {
            float sl = 0.0f, sr = 0.0f;
            for (int l = 0; l < kNumLayers; ++l)
                if (bus.L[l]) { sl += bus.L[l][i]; sr += bus.R[l][i]; }
            float rl, rr;
            reverb.process (sl, sr, rl, rr);
            outL[i] += rl * reverbReturn;
            outR[i] += rr * reverbReturn;
        }

    // ---- master: width, gain, DC block, safety soft clip
    for (int i = 0; i < n; ++i)
    {
        const float mid = 0.5f * (outL[i] + outR[i]), side = 0.5f * (outL[i] - outR[i]) * mix.width;
        outL[i] = softClip (dcL.process ((mid + side) * mix.master));
        outR[i] = softClip (dcR.process ((mid - side) * mix.master));
    }
}
} // namespace esd
