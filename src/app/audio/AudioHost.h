#pragma once
// Real-time audio host.
//
// Audio thread rules (enforced by design):
//  * the engine runtime (simulation + synthesis) is built on the message thread and handed to
//    the audio thread through an atomic pointer; the audio thread never allocates or frees:
//    the replaced runtime is crossfaded out then handed back through another atomic pointer
//    and deleted by a message-thread timer;
//  * controls, mixer values and telemetry are plain atomics (no locks);
//  * analysis samples go to the GUI through a lock-free FIFO.
#include <JuceHeader.h>

#include "model/Project.h"
#include "synth/EngineRuntime.h"

#include <array>
#include <atomic>

namespace esd
{
/// Read-only sample playback for the Sample Editor preview.
struct PreviewClip
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 48000.0;
    int start = 0, end = 0;
    bool loop = false;
    float gain = 1.0f;
};

class AudioHost : public juce::AudioIODeviceCallback, private juce::Timer
{
public:
    AudioHost();
    ~AudioHost() override;

    juce::String initialise (const juce::String& savedStateXml);
    juce::AudioDeviceManager& deviceManager() { return devices; }
    std::unique_ptr<juce::XmlElement> deviceState() const { return devices.createStateXml(); }

    /// Builds a new engine runtime for this project (message thread) and schedules the swap.
    void setProject (const Project&);
    /// Decoders used when building runtimes (samples, impulse responses): set by the app.
    EngineRuntime::Loaders loaders;
    /// Mixer changes only: no rebuild.
    void setMix (const SoundMix&);

    // ---- live controls (any thread)
    std::atomic<float> throttle { 0.0f };
    std::atomic<float> externalLoad { 0.0f };
    std::atomic<bool> ignition { false };
    std::atomic<bool> starter { false };
    std::atomic<bool> limiterEnabled { true };
    std::atomic<bool> dynoMode { false };
    std::atomic<bool> vehicleMode { false };
    std::atomic<float> brake { 0.0f };
    std::atomic<uint32_t> shiftUpCount { 0 }, shiftDownCount { 0 };
    std::atomic<float> dynoRpm { 3000.0f };
    std::atomic<float> dynoLoad { 0.5f };
    std::atomic<float> monitorGain { 1.0f };
    std::atomic<bool> engineAudible { true };

    /// Starts the engine as if it had been cranked already (instant idle).
    void startEngineInstant();
    void stopEngine();

    Telemetry telemetry() const;
    double sampleRate() const { return currentRate.load(); }
    float cpuLoad() const { return static_cast<float> (devices.getCpuUsage()); }
    bool isEngineRunning() const { return telemetrySnapshot.combustion.load(); }

    /// Pulls up to n most recent mono output samples for analysis (GUI thread).
    int readAnalysis (float* dest, int n);

    // ---- sample preview
    void playClip (std::unique_ptr<PreviewClip>);
    void stopClip();
    bool isClipPlaying() const { return clipPlaying.load(); }
    float clipPosition() const { return clipPos.load(); } ///< seconds

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void timerCallback() override;
    void publishTelemetry (const Telemetry&);

    juce::AudioDeviceManager devices;
    std::atomic<double> currentRate { 48000.0 };
    Project lastProject;
    bool haveProject = false;
    double builtRate = 0.0;

    // runtime hand-over
    std::atomic<EngineRuntime*> pending { nullptr };
    std::atomic<EngineRuntime*> retired { nullptr };
    EngineRuntime* current = nullptr; // audio thread
    EngineRuntime* fading = nullptr;  // audio thread
    int fadePos = 0;
    static constexpr int kFadeLen = 1024;
    std::atomic<int> resetRequest { 0 }; // 1 = start running, 2 = stop

    // mixer
    std::array<std::atomic<float>, kNumLayers> mixGain {};
    std::array<std::atomic<float>, kNumLayers> mixPan {};
    std::atomic<float> mixMaster { 0.5f }, mixWidth { 1.0f };

    // scratch (allocated in audioDeviceAboutToStart)
    std::vector<float> bufA, bufB, bufC, bufD;

    // telemetry
    struct AtomicTelemetry
    {
        std::atomic<float> rpm { 0 }, torque { 0 }, power { 0 }, airflow { 0 }, throttle { 0 }, boost { 0 }, egt { 0 }, coolant { 0 }, shaft { 0 }, firing { 0 };
        std::atomic<bool> combustion { false }, fuelCut { false }, limiter { false }, starter { false };
        std::array<std::atomic<float>, kNumLayers> layer {};
    } telemetrySnapshot;

    // analysis FIFO
    juce::AbstractFifo fifo { 1 << 15 };
    std::vector<float> fifoData;

    // preview
    std::atomic<PreviewClip*> clipPending { nullptr };
    std::atomic<PreviewClip*> clipRetired { nullptr };
    PreviewClip* clip = nullptr;
    double clipReadPos = 0.0;
    std::atomic<bool> clipStopRequest { false };
    std::atomic<bool> clipPlaying { false };
    std::atomic<float> clipPos { 0.0f };
};
} // namespace esd
