#pragma once
// Minimal WAV reader for the JUCE-free core (PCM 8/16/24/32, float 32/64, any channel count).
// The application uses JUCE readers (WAV, FLAC, AIFF) through the same loader callbacks.
#include <string>
#include <vector>

namespace esd
{
struct AudioData
{
    double sampleRate = 48000.0;
    std::vector<std::vector<float>> channels;
    std::size_t frames() const { return channels.empty() ? 0 : channels.front().size(); }
};

bool readWav (const std::string& path, AudioData& out, std::string* error = nullptr);
/// Linear resampling (adequate for impulse responses and previews).
std::vector<float> resampleLinear (const std::vector<float>& in, double fromRate, double toRate);
} // namespace esd
