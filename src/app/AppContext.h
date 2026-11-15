#pragma once
// Application services shared by every page: document, audio, library, settings, file flows.
#include <JuceHeader.h>

#include "ProjectDocument.h"
#include "audio/AudioHost.h"
#include "library/ProjectLibrary.h"
#include "render/LoopGenerator.h"

namespace esd
{
class AppContext : private juce::ChangeListener
{
public:
    AppContext();
    ~AppContext() override;

    ProjectDocument doc;
    AudioHost audio;
    ProjectLibrary library;
    std::unique_ptr<juce::PropertiesFile> settings;

    juce::File appDataDir, projectsDir, autosaveDir, exportsDir;

    std::function<void (const juce::String& pageId)> navigate;
    std::function<void (const juce::String& message, bool isError)> status;

    // ---- project flows (all asynchronous where a dialog is involved)
    void newFromPreset (const std::string& presetId);
    void openFile (const juce::File&);
    void openDialog();
    void save();
    void saveAsDialog();
    void importDialog();
    void exportProjectDialog();
    void duplicateFile (const juce::File&);
    /// Runs 'then' directly, or after asking to save/discard when the project has unsaved edits.
    void confirmDiscard (std::function<void()> then);
    juce::File suggestedPathFor (const Project&) const;
    void registerInLibrary (const Project&, const juce::File&);
    juce::Array<juce::File> autosavedSessions() const;

    void saveSettings();

    // ---- audio decoding / encoding through JUCE (WAV, FLAC, AIFF)
    juce::AudioFormatManager formats;
    juce::File resolvePath (const std::string& path) const;
    bool decode (const juce::File&, juce::AudioBuffer<float>&, double& rate);
    EngineRuntime::Loaders makeLoaders();
    /// AudioWriter for the loop generator / exports: WAV through the core, FLAC through JUCE.
    bool writeAudio (const std::string& pathNoExt, const StereoBuffer&, const LoopSpec&, std::string& written, std::string* error);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace esd
