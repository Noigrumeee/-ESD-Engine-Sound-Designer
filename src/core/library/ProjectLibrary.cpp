#include "ProjectLibrary.h"

#include <sqlite3.h>

#include <filesystem>

namespace esd
{
namespace
{
struct Stmt
{
    sqlite3_stmt* s = nullptr;
    Stmt (sqlite3* db, const char* sql) { sqlite3_prepare_v2 (db, sql, -1, &s, nullptr); }
    ~Stmt() { sqlite3_finalize (s); }
    bool ok() const { return s != nullptr; }
    void text (int i, const std::string& v) { sqlite3_bind_text (s, i, v.c_str(), static_cast<int> (v.size()), SQLITE_TRANSIENT); }
    void i64 (int i, int64_t v) { sqlite3_bind_int64 (s, i, v); }
    std::string col (int i) const
    {
        const auto* t = sqlite3_column_text (s, i);
        return t ? reinterpret_cast<const char*> (t) : "";
    }
};
} // namespace

ProjectLibrary::~ProjectLibrary() { close(); }

void ProjectLibrary::close()
{
    if (db) sqlite3_close (db);
    db = nullptr;
}

bool ProjectLibrary::open (const std::string& path, std::string* error)
{
    close();
    std::error_code ec;
    const std::filesystem::path p (reinterpret_cast<const char8_t*> (path.c_str()));
    if (p.has_parent_path()) std::filesystem::create_directories (p.parent_path(), ec);
    if (sqlite3_open (path.c_str(), &db) != SQLITE_OK)
    {
        err = db ? sqlite3_errmsg (db) : "sqlite3_open failed";
        if (error) *error = err;
        close();
        return false;
    }
    sqlite3_busy_timeout (db, 2000);
    const bool ok = exec ("PRAGMA journal_mode=WAL;")
                    && exec ("CREATE TABLE IF NOT EXISTS projects ("
                             " id TEXT PRIMARY KEY, name TEXT NOT NULL, category TEXT, kind TEXT, path TEXT NOT NULL,"
                             " description TEXT, modified INTEGER DEFAULT 0, opened INTEGER DEFAULT 0, favorite INTEGER DEFAULT 0);")
                    && exec ("CREATE INDEX IF NOT EXISTS projects_opened ON projects(opened DESC);")
                    && exec ("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);")
                    && exec ("INSERT OR IGNORE INTO meta(key, value) VALUES('schema', '1');");
    if (! ok && error) *error = err;
    return ok;
}

bool ProjectLibrary::exec (const char* sql)
{
    char* msg = nullptr;
    if (sqlite3_exec (db, sql, nullptr, nullptr, &msg) != SQLITE_OK)
    {
        err = msg ? msg : "sqlite error";
        sqlite3_free (msg);
        return false;
    }
    return true;
}

bool ProjectLibrary::upsert (const LibraryEntry& e)
{
    if (! db) return false;
    Stmt st (db, "INSERT INTO projects(id,name,category,kind,path,description,modified,opened,favorite) VALUES(?,?,?,?,?,?,?,?,?)"
                 " ON CONFLICT(id) DO UPDATE SET name=excluded.name, category=excluded.category, kind=excluded.kind, path=excluded.path,"
                 " description=excluded.description, modified=excluded.modified,"
                 " opened=MAX(projects.opened, excluded.opened);");
    if (! st.ok()) { err = sqlite3_errmsg (db); return false; }
    st.text (1, e.id); st.text (2, e.name); st.text (3, e.category); st.text (4, e.kind); st.text (5, e.path); st.text (6, e.description);
    st.i64 (7, e.modified); st.i64 (8, e.opened); st.i64 (9, e.favorite ? 1 : 0);
    if (sqlite3_step (st.s) != SQLITE_DONE) { err = sqlite3_errmsg (db); return false; }
    return true;
}

bool ProjectLibrary::remove (const std::string& id)
{
    if (! db) return false;
    Stmt st (db, "DELETE FROM projects WHERE id=?;");
    st.text (1, id);
    return sqlite3_step (st.s) == SQLITE_DONE;
}

bool ProjectLibrary::markOpened (const std::string& id, int64_t when)
{
    if (! db) return false;
    Stmt st (db, "UPDATE projects SET opened=? WHERE id=?;");
    st.i64 (1, when);
    st.text (2, id);
    return sqlite3_step (st.s) == SQLITE_DONE;
}

bool ProjectLibrary::setFavorite (const std::string& id, bool fav)
{
    if (! db) return false;
    Stmt st (db, "UPDATE projects SET favorite=? WHERE id=?;");
    st.i64 (1, fav ? 1 : 0);
    st.text (2, id);
    return sqlite3_step (st.s) == SQLITE_DONE;
}

std::vector<LibraryEntry> ProjectLibrary::query (const char* sql, int limit) const
{
    std::vector<LibraryEntry> out;
    if (! db) return out;
    Stmt st (db, sql);
    if (! st.ok()) { err = sqlite3_errmsg (db); return out; }
    if (limit > 0) st.i64 (1, limit);
    while (sqlite3_step (st.s) == SQLITE_ROW)
    {
        LibraryEntry e;
        e.id = st.col (0); e.name = st.col (1); e.category = st.col (2); e.kind = st.col (3); e.path = st.col (4); e.description = st.col (5);
        e.modified = sqlite3_column_int64 (st.s, 6);
        e.opened = sqlite3_column_int64 (st.s, 7);
        e.favorite = sqlite3_column_int64 (st.s, 8) != 0;
        out.push_back (e);
    }
    return out;
}

std::vector<LibraryEntry> ProjectLibrary::all() const
{
    return query ("SELECT id,name,category,kind,path,description,modified,opened,favorite FROM projects ORDER BY category, name;", 0);
}

std::vector<LibraryEntry> ProjectLibrary::recent (int limit) const
{
    return query ("SELECT id,name,category,kind,path,description,modified,opened,favorite FROM projects WHERE opened>0 ORDER BY opened DESC LIMIT ?;", limit);
}

std::vector<std::string> ProjectLibrary::categories() const
{
    std::vector<std::string> out;
    if (! db) return out;
    Stmt st (db, "SELECT DISTINCT category FROM projects ORDER BY category;");
    while (sqlite3_step (st.s) == SQLITE_ROW) out.push_back (st.col (0));
    return out;
}

int ProjectLibrary::pruneMissingFiles()
{
    int removed = 0;
    for (auto& e : all())
    {
        std::error_code ec;
        if (! std::filesystem::exists (std::filesystem::path (reinterpret_cast<const char8_t*> (e.path.c_str())), ec))
            if (remove (e.id)) ++removed;
    }
    return removed;
}
} // namespace esd
