#pragma once
// Engine library index (SQLite). Project contents stay in their .esdproj JSON files; the
// database only indexes them (name, category, kind, path, dates, favourites, recent list),
// so a damaged database can always be rebuilt by rescanning the library folder.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace esd
{
struct LibraryEntry
{
    std::string id;
    std::string name;
    std::string category;
    std::string kind;
    std::string path;
    std::string description;
    int64_t modified = 0; ///< unix seconds
    int64_t opened = 0;
    bool favorite = false;
};

class ProjectLibrary
{
public:
    ProjectLibrary() = default;
    ~ProjectLibrary();
    ProjectLibrary (const ProjectLibrary&) = delete;
    ProjectLibrary& operator= (const ProjectLibrary&) = delete;

    bool open (const std::string& dbPath, std::string* error = nullptr);
    bool isOpen() const { return db != nullptr; }
    void close();

    bool upsert (const LibraryEntry&);
    bool remove (const std::string& id);
    bool markOpened (const std::string& id, int64_t when);
    bool setFavorite (const std::string& id, bool fav);
    std::vector<LibraryEntry> all() const;
    std::vector<LibraryEntry> recent (int limit) const;
    std::vector<std::string> categories() const;
    /// Removes entries whose file no longer exists. Returns how many were removed.
    int pruneMissingFiles();

    std::string lastError() const { return err; }

private:
    bool exec (const char* sql);
    std::vector<LibraryEntry> query (const char* sql, int limit) const;
    sqlite3* db = nullptr;
    mutable std::string err;
};
} // namespace esd
