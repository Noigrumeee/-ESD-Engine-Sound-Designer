#include "AppContext.h"

#include "model/Presets.h"
#include "model/Serialization.h"
#include "render/AudioFile.h"

namespace esd
{
namespace
{
juce::String sanitizeFileName (const juce::String& s)
{
    auto r = juce::File::createLegalFileName (s.trim());
    return r.isEmpty() ? juce::String ("Engine") : r;
}
} // namespace

AppContext::AppContext()
{
    appDataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("EngineSoundDesigner");
    projectsDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("EngineSoundDesigner").getChildFile ("Projects");
    exportsDir = projectsDir.getParentDirectory().getChildFile ("Exports");
    autosaveDir = appDataDir.getChildFile ("autosave");
    appDataDir.createDirectory();
    projectsDir.createDirectory();
    autosaveDir.createDirectory();

    juce::PropertiesFile::Options opt;
    opt.applicationName = "EngineSoundDesigner";
    opt.filenameSuffix = ".settings";
    opt.folderName = "EngineSoundDesigner";
    opt.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opt);

    std::string err;
    if (! library.open (appDataDir.getChildFile ("library.db").getFullPathName().toStdString(), &err))
        juce::Logger::writeToLog ("library: " + juce::String (err));
    library.pruneMissingFiles();
    if (settings->getIntValue ("presetsVersion", settings->getBoolValue ("presetsSeeded", false) ? 1 : 0) < 2)
    {
        // First launch: the demo engines become real, editable project files in the library.
        const auto dir = projectsDir.getChildFile ("Demo presets");
        dir.createDirectory();
        for (auto& info : builtinPresets())
        {
            Project p = makePreset (info.id);
            const auto f = dir.getChildFile (sanitizeFileName (p.name) + ".esdproj");
            if (! f.existsAsFile() && saveProjectFile (p, f.getFullPathName().toStdString()))
                registerInLibrary (p, f);
        }
        settings->setValue ("presetsVersion", 2); // 2 = electric + hybrid presets added (only missing files are written)
        settings->saveIfNeeded();
    }

    doc.setAutosaveFolder (autosaveDir);
    doc.onSaved = [this] (const Project& p, const juce::File& f) { registerInLibrary (p, f); };
    doc.addChangeListener (this);

    formats.registerBasicFormats();
    audio.loaders = makeLoaders();
    const auto audioErr = audio.initialise (settings->getValue ("audioDevice"));
    if (audioErr.isNotEmpty()) juce::Logger::writeToLog ("audio: " + audioErr);
    audio.setProject (doc.project());
}

AppContext::~AppContext()
{
    doc.removeChangeListener (this);
    saveSettings();
}

juce::File AppContext::resolvePath (const std::string& path) const
{
    const juce::String p (juce::CharPointer_UTF8 (path.c_str()));
    if (juce::File::isAbsolutePath (p)) return juce::File (p);
    const auto base = doc.file() != juce::File() ? doc.file().getParentDirectory() : projectsDir;
    return base.getChildFile (p);
}

bool AppContext::decode (const juce::File& f, juce::AudioBuffer<float>& buf, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (! r) return false;
    const auto len = static_cast<int> (juce::jmin<juce::int64> (r->lengthInSamples, static_cast<juce::int64> (r->sampleRate * 600.0)));
    buf.setSize (static_cast<int> (juce::jlimit (1u, 2u, r->numChannels)), len);
    r->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
    rate = r->sampleRate;
    return true;
}

EngineRuntime::Loaders AppContext::makeLoaders()
{
    EngineRuntime::Loaders l;
    l.samples = [this] (const SampleRef& s, LoadedSample& out)
    {
        juce::AudioBuffer<float> buf;
        double rate = 48000.0;
        if (! decode (resolvePath (s.path), buf, rate)) return false;
        out.sampleRate = rate;
        out.L.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        if (buf.getNumChannels() > 1) out.R.assign (buf.getReadPointer (1), buf.getReadPointer (1) + buf.getNumSamples());
        return true;
    };
    l.ir = [this] (const std::string& path, double targetRate)
    {
        juce::AudioBuffer<float> buf;
        double rate = 48000.0;
        if (! decode (resolvePath (path), buf, rate)) return std::vector<float> {};
        std::vector<float> mono (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
        return resampleLinear (mono, rate, targetRate);
    };
    return l;
}

bool AppContext::writeAudio (const std::string& pathNoExt, const StereoBuffer& b, const LoopSpec& spec, std::string& written, std::string* error)
{
    if (! spec.flac) return defaultAudioWriter() (pathNoExt, b, spec, written, error);
    const juce::File f (juce::String (juce::CharPointer_UTF8 ((pathNoExt + ".flac").c_str())));
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (! os) { if (error) *error = "cannot write " + f.getFullPathName().toStdString(); return false; }
    std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os.get(), b.sampleRate, 2, juce::jmin (24, spec.bitDepth), {}, 0));
    if (! w) { if (error) *error = "FLAC writer unavailable"; return false; }
    os.release();
    const float* ch[2] = { b.left.data(), b.right.data() };
    w->writeFromFloatArrays (ch, 2, static_cast<int> (b.frames()));
    written = (pathNoExt + ".flac");
    return true;
}

void AppContext::saveSettings()
{
    if (auto xml = audio.deviceState()) settings->setValue ("audioDevice", xml->toString());
    settings->saveIfNeeded();
}

void AppContext::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (doc.lastChangeKind() == ProjectDocument::ChangeKind::MixOnly)
        audio.setMix (doc.project().mix);
    else
        audio.setProject (doc.project());
    for (auto& n : doc.lastNotes())
        if (status) status (n, true);
}

juce::File AppContext::suggestedPathFor (const Project& p) const
{
    auto f = projectsDir.getChildFile (sanitizeFileName (p.name) + ".esdproj");
    return f.exists() ? f.getNonexistentSibling (false) : f;
}

void AppContext::registerInLibrary (const Project& p, const juce::File& f)
{
    LibraryEntry e;
    e.id = p.id;
    e.name = p.name;
    e.category = p.category;
    e.kind = toString (p.kind);
    e.path = f.getFullPathName().toStdString();
    e.description = p.description;
    e.modified = juce::Time::getCurrentTime().toMilliseconds() / 1000;
    e.opened = e.modified;
    library.upsert (e);
}

void AppContext::confirmDiscard (std::function<void()> then)
{
    if (! doc.isDirty())
    {
        then();
        return;
    }
    auto opts = juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Unsaved changes")
                    .withMessage ("\"" + juce::String (doc.project().name) + "\" has unsaved changes.")
                    .withButton ("Save")
                    .withButton ("Discard")
                    .withButton ("Cancel");
    juce::AlertWindow::showAsync (opts, [this, then] (int result)
    {
        if (result == 1)
        {
            save();
            if (! doc.isDirty()) then();
        }
        else if (result == 2)
        {
            doc.deleteAutosave();
            then();
        }
    });
}

void AppContext::newFromPreset (const std::string& presetId)
{
    confirmDiscard ([this, presetId]
    {
        Project p = makePreset (presetId);
        doc.load (p, {});
        if (status) status ("New project from preset: " + juce::String (p.name) + " (not saved yet)", false);
        if (navigate) navigate ("builder");
    });
}

void AppContext::openFile (const juce::File& f)
{
    confirmDiscard ([this, f]
    {
        auto r = loadProjectFile (f.getFullPathName().toStdString());
        if (! r.ok)
        {
            if (status) status ("Cannot open " + f.getFileName() + ": " + juce::String (r.error), true);
            return;
        }
        const bool fromAutosave = f.isAChildOf (autosaveDir);
        doc.load (r.project, fromAutosave ? juce::File() : f);
        if (! fromAutosave) registerInLibrary (r.project, f);
        for (auto& fix : r.fixes)
            if (status) status (juce::String (fix.message), fix.severity == Severity::Error);
        if (status) status ((fromAutosave ? "Recovered " : "Opened ") + juce::String (r.project.name), false);
        if (navigate) navigate ("builder");
    });
}

void AppContext::openDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open engine project", projectsDir, "*.esdproj");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File()) openFile (fc.getResult());
    });
}

void AppContext::save()
{
    if (doc.file() == juce::File())
    {
        // first save goes straight to the library folder (no dialog needed)
        juce::String err;
        if (! doc.saveAs (suggestedPathFor (doc.project()), &err))
        {
            if (status) status ("Save failed: " + err, true);
        }
        else if (status)
            status ("Saved to " + doc.file().getFullPathName(), false);
        return;
    }
    juce::String err;
    if (! doc.save (&err)) { if (status) status ("Save failed: " + err, true); }
    else if (status) status ("Saved", false);
}

void AppContext::saveAsDialog()
{
    const auto start = doc.file() != juce::File() ? doc.file() : suggestedPathFor (doc.project());
    chooser = std::make_unique<juce::FileChooser> ("Save engine project as", start, "*.esdproj");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension (".esdproj");
        juce::String err;
        if (! doc.saveAs (f, &err)) { if (status) status ("Save failed: " + err, true); }
        else if (status) status ("Saved to " + f.getFullPathName(), false);
    });
}

void AppContext::importDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Import engine projects into the library", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.esdproj");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        int ok = 0;
        for (auto& f : fc.getResults())
        {
            auto r = loadProjectFile (f.getFullPathName().toStdString());
            if (! r.ok) { if (status) status ("Skipped " + f.getFileName() + ": " + juce::String (r.error), true); continue; }
            r.project.id = newUuid(); // imported copy gets its own identity
            const auto dest = suggestedPathFor (r.project);
            if (saveProjectFile (r.project, dest.getFullPathName().toStdString()))
            {
                registerInLibrary (r.project, dest);
                ++ok;
            }
        }
        if (status) status ("Imported " + juce::String (ok) + " project(s) into the library", false);
    });
}

void AppContext::exportProjectDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Export project file", exportsDir.getChildFile (sanitizeFileName (doc.project().name) + ".esdproj"), "*.esdproj");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        std::string err;
        if (saveProjectFile (doc.project(), f.withFileExtension (".esdproj").getFullPathName().toStdString(), &err))
        {
            if (status) status ("Exported " + f.getFileName(), false);
        }
        else if (status)
            status ("Export failed: " + juce::String (err), true);
    });
}

void AppContext::duplicateFile (const juce::File& f)
{
    auto r = loadProjectFile (f.getFullPathName().toStdString());
    if (! r.ok) { if (status) status ("Cannot duplicate: " + juce::String (r.error), true); return; }
    r.project.id = newUuid();
    r.project.name += " (copy)";
    const auto dest = suggestedPathFor (r.project);
    if (saveProjectFile (r.project, dest.getFullPathName().toStdString()))
    {
        registerInLibrary (r.project, dest);
        if (status) status ("Duplicated as " + dest.getFileName(), false);
    }
}

juce::Array<juce::File> AppContext::autosavedSessions() const
{
    juce::Array<juce::File> out;
    for (auto& f : autosaveDir.findChildFiles (juce::File::findFiles, false, "*.esdproj"))
        if (f.getFileNameWithoutExtension() != juce::String (doc.project().id)) out.add (f);
    return out;
}
} // namespace esd
