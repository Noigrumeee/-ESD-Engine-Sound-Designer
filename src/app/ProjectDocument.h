#pragma once
// The open project: current state, file, dirty flag, undo/redo (JSON snapshots), autosave.
// Message thread only.
#include <JuceHeader.h>

#include "model/Project.h"

namespace esd
{
class ProjectDocument : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    enum class ChangeKind { Structure, MixOnly, Loaded };

    ProjectDocument();
    ~ProjectDocument() override;

    const Project& project() const { return proj; }

    /// Applies an edit. coalesceKey groups rapid edits (slider drags) into one undo step.
    void edit (const std::function<void (Project&)>& fn, const juce::String& description, const juce::String& coalesceKey = {}, bool mixOnly = false);

    void load (const Project&, const juce::File& file);
    bool save (juce::String* error = nullptr);
    bool saveAs (const juce::File&, juce::String* error = nullptr);

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    juce::String undoDescription() const { return undoStack.empty() ? juce::String() : undoStack.back().second; }

    bool isDirty() const { return dirty; }
    juce::File file() const { return path; }
    ChangeKind lastChangeKind() const { return lastKind; }
    /// Messages from the last load/sanitize (shown to the user).
    juce::StringArray lastNotes() const { return notes; }

    void setAutosaveFolder (const juce::File& f) { autosaveDir = f; }
    juce::File autosaveFile() const;
    void deleteAutosave();

    std::function<void (const Project&, const juce::File&)> onSaved;

private:
    void timerCallback() override;
    void changed (ChangeKind);

    Project proj;
    juce::File path;
    bool dirty = false;
    std::vector<std::pair<std::string, juce::String>> undoStack, redoStack;
    juce::String lastCoalesce;
    juce::uint32 lastEditMs = 0;
    ChangeKind lastKind = ChangeKind::Loaded;
    juce::StringArray notes;
    juce::File autosaveDir;
    bool autosavePending = false;
    static constexpr std::size_t kMaxUndo = 300;
};
} // namespace esd
