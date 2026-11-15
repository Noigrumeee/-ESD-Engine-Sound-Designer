#include "ProjectDocument.h"

#include "model/Presets.h"
#include "model/Serialization.h"

namespace esd
{
ProjectDocument::ProjectDocument()
{
    proj = makePreset ("i4_na");
    startTimer (30 * 1000); // autosave check every 30 s
}

ProjectDocument::~ProjectDocument() { stopTimer(); }

void ProjectDocument::changed (ChangeKind k)
{
    lastKind = k;
    sendChangeMessage();
}

void ProjectDocument::edit (const std::function<void (Project&)>& fn, const juce::String& description, const juce::String& coalesceKey, bool mixOnly)
{
    const auto now = juce::Time::getMillisecondCounter();
    const bool coalesce = coalesceKey.isNotEmpty() && coalesceKey == lastCoalesce && now - lastEditMs < 1500;
    if (! coalesce)
    {
        undoStack.emplace_back (projectToJson (proj, -1), description);
        if (undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());
        redoStack.clear();
    }
    lastCoalesce = coalesceKey;
    lastEditMs = now;

    fn (proj);
    if (! mixOnly)
    {
        // keep the model consistent (ranges, firing order, limiter...) after every edit
        auto fixes = sanitize (proj);
        notes.clear();
        for (auto& f : fixes)
            if (f.severity == Severity::Error) notes.add (f.message);
    }
    dirty = true;
    autosavePending = true;
    changed (mixOnly ? ChangeKind::MixOnly : ChangeKind::Structure);
}

void ProjectDocument::load (const Project& p, const juce::File& f)
{
    proj = p;
    path = f;
    dirty = false;
    undoStack.clear();
    redoStack.clear();
    lastCoalesce = {};
    notes.clear();
    changed (ChangeKind::Loaded);
}

bool ProjectDocument::save (juce::String* error)
{
    if (path == juce::File()) return false;
    return saveAs (path, error);
}

bool ProjectDocument::saveAs (const juce::File& f, juce::String* error)
{
    std::string err;
    if (! saveProjectFile (proj, f.getFullPathName().toStdString(), &err))
    {
        if (error) *error = err;
        return false;
    }
    path = f;
    dirty = false;
    deleteAutosave();
    if (onSaved) onSaved (proj, path);
    changed (ChangeKind::MixOnly); // title / dirty state refresh only
    return true;
}

void ProjectDocument::undo()
{
    if (undoStack.empty()) return;
    redoStack.emplace_back (projectToJson (proj, -1), undoStack.back().second);
    auto r = projectFromJson (undoStack.back().first);
    undoStack.pop_back();
    if (r.ok) proj = r.project;
    lastCoalesce = {};
    dirty = true;
    changed (ChangeKind::Structure);
}

void ProjectDocument::redo()
{
    if (redoStack.empty()) return;
    undoStack.emplace_back (projectToJson (proj, -1), redoStack.back().second);
    auto r = projectFromJson (redoStack.back().first);
    redoStack.pop_back();
    if (r.ok) proj = r.project;
    lastCoalesce = {};
    dirty = true;
    changed (ChangeKind::Structure);
}

juce::File ProjectDocument::autosaveFile() const
{
    if (autosaveDir == juce::File()) return {};
    return autosaveDir.getChildFile (juce::String (proj.id) + ".esdproj");
}

void ProjectDocument::deleteAutosave()
{
    auto f = autosaveFile();
    if (f.existsAsFile()) f.deleteFile();
}

void ProjectDocument::timerCallback()
{
    if (! autosavePending || ! dirty) return;
    auto f = autosaveFile();
    if (f == juce::File()) return;
    f.getParentDirectory().createDirectory();
    if (saveProjectFile (proj, f.getFullPathName().toStdString()))
        autosavePending = false;
}
} // namespace esd
