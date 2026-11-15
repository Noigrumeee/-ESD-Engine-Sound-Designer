#include "AudioHost.h"

namespace esd
{
AudioHost::AudioHost()
{
    fifoData.assign (static_cast<std::size_t> (fifo.getTotalSize()), 0.0f);
    for (auto& g : mixGain) g.store (1.0f);
    for (auto& p : mixPan) p.store (0.0f);
}

AudioHost::~AudioHost()
{
    stopTimer();
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
    delete current;
    delete fading;
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete clip;
    delete clipPending.exchange (nullptr);
    delete clipRetired.exchange (nullptr);
}

juce::String AudioHost::initialise (const juce::String& savedStateXml)
{
    std::unique_ptr<juce::XmlElement> xml;
    if (savedStateXml.isNotEmpty()) xml = juce::parseXML (savedStateXml);

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = 48000.0;
    preferred.bufferSize = 256;
    if (! xml)
    {
        // Prefer WASAPI ("Windows Audio") for low latency when no saved setup exists.
        for (auto* type : devices.getAvailableDeviceTypes())
            if (type->getTypeName() == "Windows Audio")
                devices.setCurrentAudioDeviceType (type->getTypeName(), false);
    }
    auto err = devices.initialise (0, 2, xml.get(), true, {}, xml ? nullptr : &preferred);
    devices.addAudioCallback (this);
    startTimer (40);
    return err;
}

void AudioHost::setProject (const Project& p)
{
    lastProject = p;
    haveProject = true;
    builtRate = currentRate.load();
    auto* rt = new EngineRuntime (p, builtRate, loaders);
    delete pending.exchange (rt); // a runtime the audio thread never took can be deleted here
    setMix (p.mix);
}

void AudioHost::setMix (const SoundMix& m)
{
    const auto v = mixFromProject (m);
    for (int i = 0; i < kNumLayers; ++i)
    {
        mixGain[static_cast<std::size_t> (i)].store (v.gain[static_cast<std::size_t> (i)]);
        mixPan[static_cast<std::size_t> (i)].store (v.pan[static_cast<std::size_t> (i)]);
    }
    mixMaster.store (v.master);
    mixWidth.store (v.width);
    lastProject.mix = m;
}

void AudioHost::startEngineInstant()
{
    ignition.store (true);
    resetRequest.store (1);
}

void AudioHost::stopEngine()
{
    ignition.store (false);
    starter.store (false);
}

void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    const int block = juce::jmax (256, device->getCurrentBufferSizeSamples());
    bufA.assign (static_cast<std::size_t> (block), 0.0f);
    bufB.assign (static_cast<std::size_t> (block), 0.0f);
    bufC.assign (static_cast<std::size_t> (block), 0.0f);
    bufD.assign (static_cast<std::size_t> (block), 0.0f);
    currentRate.store (rate); // the timer rebuilds the runtime when the rate changed
}

void AudioHost::audioDeviceStopped() {}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch]) juce::FloatVectorOperations::clear (out[ch], numSamples);
    if (numOut <= 0 || bufA.empty()) return;

    // ---- runtime hand-over (lock-free)
    if (fading == nullptr && retired.load() == nullptr)
    {
        if (auto* p = pending.exchange (nullptr))
        {
            if (current) p->adoptFrom (*current);
            fading = current;
            current = p;
            fadePos = fading ? 0 : kFadeLen;
        }
    }
    if (const int r = resetRequest.exchange (0); r == 1 && current)
        current->resetEngine (true);

    EngineControls c;
    c.throttle = throttle.load();
    c.externalLoad = externalLoad.load();
    c.ignition = ignition.load();
    c.starter = starter.load();
    c.limiterEnabled = limiterEnabled.load();
    c.mode = dynoMode.load() ? DriveMode::Dyno : vehicleMode.load() ? DriveMode::Vehicle : DriveMode::FreeRev;
    c.brake = brake.load();
    c.shiftUpCount = shiftUpCount.load();
    c.shiftDownCount = shiftDownCount.load();
    c.dynoRpm = dynoRpm.load();
    c.dynoLoad = dynoLoad.load();

    VoiceMix mix;
    for (int i = 0; i < kNumLayers; ++i)
    {
        mix.gain[static_cast<std::size_t> (i)] = mixGain[static_cast<std::size_t> (i)].load();
        mix.pan[static_cast<std::size_t> (i)] = mixPan[static_cast<std::size_t> (i)].load();
    }
    mix.master = mixMaster.load();
    mix.width = mixWidth.load();
    const float monitor = monitorGain.load() * (engineAudible.load() ? 1.0f : 0.0f);

    const int maxBlock = static_cast<int> (bufA.size());
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (maxBlock, numSamples - done);
        float* L = bufA.data();
        float* R = bufB.data();
        if (current)
            current->process (c, mix, L, R, n);
        else
        {
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
        }
        if (fading)
        {
            fading->process (c, mix, bufC.data(), bufD.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const float g = juce::jlimit (0.0f, 1.0f, static_cast<float> (fadePos + i) / kFadeLen);
                L[i] = L[i] * g + bufC[static_cast<std::size_t> (i)] * (1.0f - g);
                R[i] = R[i] * g + bufD[static_cast<std::size_t> (i)] * (1.0f - g);
            }
            fadePos += n;
            if (fadePos >= kFadeLen)
            {
                retired.store (fading);
                fading = nullptr;
            }
        }

        // ---- sample preview
        if (clipStopRequest.load() && clip && clipRetired.load() == nullptr)
        {
            clipStopRequest.store (false);
            clipRetired.store (clip);
            clip = nullptr;
            clipPlaying.store (false);
        }
        if (clipRetired.load() == nullptr)
            if (auto* p = clipPending.exchange (nullptr))
            {
                if (clip) clipRetired.store (clip);
                clip = p;
                clipReadPos = p->start;
                clipPlaying.store (true);
                clipStopRequest.store (false);
            }
        if (clip && clipPlaying.load())
        {
            const double step = clip->sampleRate / currentRate.load();
            const int chans = clip->buffer.getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                auto idx = static_cast<int> (clipReadPos);
                if (idx + 1 >= clip->end)
                {
                    if (clip->loop) { clipReadPos = clip->start; idx = clip->start; }
                    else { clipPlaying.store (false); break; }
                }
                const float f = static_cast<float> (clipReadPos - idx);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* src = clip->buffer.getReadPointer (juce::jmin (ch, chans - 1));
                    const float v = (src[idx] + (src[idx + 1] - src[idx]) * f) * clip->gain;
                    (ch == 0 ? L : R)[i] += v;
                }
                clipReadPos += step;
            }
            clipPos.store (static_cast<float> (clipReadPos / clip->sampleRate));
        }

        // ---- analysis FIFO (project output, before the monitor volume)
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fifoData[static_cast<std::size_t> (s1 + i)] = 0.5f * (L[i] + R[i]);
        for (int i = 0; i < n2; ++i) fifoData[static_cast<std::size_t> (s2 + i)] = 0.5f * (L[n1 + i] + R[n1 + i]);
        fifo.finishedWrite (n1 + n2);

        // ---- output
        for (int i = 0; i < n; ++i)
        {
            L[i] *= monitor;
            R[i] *= monitor;
        }
        if (numOut >= 2)
        {
            if (out[0]) juce::FloatVectorOperations::copy (out[0] + done, L, n);
            if (out[1]) juce::FloatVectorOperations::copy (out[1] + done, R, n);
        }
        else if (out[0])
            for (int i = 0; i < n; ++i) out[0][done + i] = 0.5f * (L[i] + R[i]);

        done += n;
    }
    if (current) publishTelemetry (current->telemetry());
}

void AudioHost::publishTelemetry (const Telemetry& t)
{
    auto& s = telemetrySnapshot;
    s.rpm.store (t.rpm); s.torque.store (t.brakeTorque); s.power.store (t.powerKw); s.airflow.store (t.airflow);
    s.throttle.store (t.throttle); s.boost.store (t.boostBar); s.egt.store (t.egtK); s.coolant.store (t.coolantC);
    s.shaft.store (t.turboShaft); s.firing.store (t.firingHz);
    s.combustion.store (t.combustion); s.fuelCut.store (t.fuelCut); s.limiter.store (t.limiter); s.starter.store (t.starter);
    for (int i = 0; i < kNumLayers; ++i) s.layer[static_cast<std::size_t> (i)].store (t.layerRms[i]);
}

Telemetry AudioHost::telemetry() const
{
    const auto& s = telemetrySnapshot;
    Telemetry t;
    t.rpm = s.rpm.load(); t.brakeTorque = s.torque.load(); t.powerKw = s.power.load(); t.airflow = s.airflow.load();
    t.throttle = s.throttle.load(); t.boostBar = s.boost.load(); t.egtK = s.egt.load(); t.coolantC = s.coolant.load();
    t.turboShaft = s.shaft.load(); t.firingHz = s.firing.load();
    t.combustion = s.combustion.load(); t.fuelCut = s.fuelCut.load(); t.limiter = s.limiter.load(); t.starter = s.starter.load();
    for (int i = 0; i < kNumLayers; ++i) t.layerRms[i] = s.layer[static_cast<std::size_t> (i)].load();
    return t;
}

int AudioHost::readAnalysis (float* dest, int n)
{
    const int ready = fifo.getNumReady();
    if (ready > n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready - n, s1, n1, s2, n2);
        fifo.finishedRead (n1 + n2);
    }
    int s1, n1, s2, n2;
    fifo.prepareToRead (juce::jmin (n, fifo.getNumReady()), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) dest[i] = fifoData[static_cast<std::size_t> (s1 + i)];
    for (int i = 0; i < n2; ++i) dest[n1 + i] = fifoData[static_cast<std::size_t> (s2 + i)];
    fifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void AudioHost::playClip (std::unique_ptr<PreviewClip> c)
{
    if (! c || c->buffer.getNumSamples() < 2) return;
    c->start = juce::jlimit (0, c->buffer.getNumSamples() - 2, c->start);
    c->end = juce::jlimit (c->start + 2, c->buffer.getNumSamples(), c->end <= 0 ? c->buffer.getNumSamples() : c->end);
    delete clipPending.exchange (c.release());
}

void AudioHost::stopClip() { clipStopRequest.store (true); }

void AudioHost::timerCallback()
{
    delete retired.exchange (nullptr);
    delete clipRetired.exchange (nullptr);
    // sample-rate change: rebuild the runtime at the new rate
    if (haveProject)
    {
        if (currentRate.load() != builtRate)
            setProject (lastProject);
    }
}
} // namespace esd
