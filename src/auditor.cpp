// Health scoring, live-process mapping, and lifecycle advice.
#include "dev_atlas/auditor.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unistd.h>

namespace dev_atlas {
namespace fs = std::filesystem;

// Rebuildable bloat / VCS dirs to skip when walking source trees.
static bool is_bloat_name(const std::string& n) {
    static const char* names[] = {".git", ".worktrees", ".venv", "node_modules",
                                   "build", "target", "out", ".ruff_cache",
                                   ".pytest_cache"};
    for (const char* x : names)
        if (n == x)
            return true;
    return n.rfind("venv", 0) == 0;
}
static bool path_has_bloat(const fs::path& p) {
    for (const auto& c : p)
        if (is_bloat_name(c.string()))
            return true;
    return false;
}
static bool is_source_ext(const std::string& e) {
    static const char* exts[] = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh",
                                  ".py", ".js", ".jsx", ".ts", ".tsx", ".rs", ".go",
                                  ".sh", ".lua", ".ino", ".qml", ".gd"};
    for (const char* x : exts)
        if (e == x)
            return true;
    return false;
}
// Binary heuristic: a NUL byte in the first 8KB.
static bool is_binary(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return true;
    char buf[8192];
    f.read(buf, sizeof buf);
    for (std::streamsize i = 0; i < f.gcount(); ++i)
        if (buf[i] == '\0')
            return true;
    return false;
}
static bool file_contains(const fs::path& p, const std::string& pat) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return content.find(pat) != std::string::npos;
}
static std::string find_in_path(const std::string& exe) {
    const char* pe = std::getenv("PATH");
    if (!pe)
        return "";
    std::stringstream ss(pe);
    std::string entry;
    while (std::getline(ss, entry, ':')) {
        const fs::path p = (entry.empty() ? fs::path(".") : fs::path(entry)) / exe;
        std::error_code ec;
        if (fs::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0)
            return p.string();
    }
    return "";
}
static long long count_ruleid(const fs::path& p) {
    std::ifstream f(p);
    if (!f)
        return 0;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    long long n = 0;
    size_t pos = 0;
    while ((pos = content.find("RuleID", pos)) != std::string::npos) {
        ++n;
        pos += 1;
    }
    return n;
}
static HealthCheck doc_check(const Repo& r, const char* fname, const char* label) {
    HealthCheck c;
    c.name = label;
    c.pass = fs::exists(fs::path(r.path) / fname);
    c.detail = c.pass ? "present" : "missing";
    return c;
}
static std::vector<TopFile> collect_top_files(const Repo& r) {
    static const char* dirs[] = {"src", "include", "scripts", "lib"};
    std::vector<TopFile> files;
    for (const char* d : dirs) {
        const fs::path root = fs::path(r.path) / d;
        std::error_code ec;
        if (!fs::is_directory(root, ec))
            continue;
        for (auto it = fs::recursive_directory_iterator(root,
                 fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); ++it) {
            std::error_code fec;
            if (!it->is_regular_file(fec) || path_has_bloat(it->path()))
                continue;
            const std::string ext = it->path().extension().string();
            if (!is_source_ext(ext))
                continue;
            TopFile tf;
            tf.display = collapse_home(it->path().string());
            tf.lines = count_lines(it->path());
            files.push_back(tf);
        }
    }
    std::sort(files.begin(), files.end(),
              [](const TopFile& a, const TopFile& b) { return a.lines > b.lines; });
    return files;
}
static HealthCheck check_ceiling(Repo& r) {
    const std::vector<TopFile> files = collect_top_files(r);
    const size_t n = std::min<size_t>(3, files.size());
    r.top_files.clear();
    for (size_t i = 0; i < n; ++i)
        r.top_files.push_back(files[i]);
    bool over = false;
    for (const auto& f : files)
        if (f.lines > 350) {
            over = true;
            break;
        }
    HealthCheck c;
    c.name = "line ceiling";
    c.pass = !over;
    if (files.empty()) {
        c.detail = "no source files";
    } else {
        std::string d = "top: ";
        for (size_t i = 0; i < n; ++i)
            d += (i ? ", " : "") + files[i].display + " " + std::to_string(files[i].lines);
        c.detail = d;
    }
    return c;
}
static void scan_hardcoded(const fs::path& dir, int depth, const std::string& pat,
                           std::vector<std::string>& found) {
    if (depth > 4 || found.size() >= 5)
        return;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::directory_iterator(); ++it) {
        const fs::path p = it->path();
        const std::string name = p.filename().string();
        if (it->is_directory(ec)) {
            if (is_bloat_name(name))
                continue;
            scan_hardcoded(p, depth + 1, pat, found);
            if (found.size() >= 5)
                return;
        } else if (it->is_regular_file(ec)) {
            if (it->file_size(ec) > 2048LL * 1024 || is_binary(p))
                continue;
            if (file_contains(p, pat))
                found.push_back(collapse_home(p.string()));
        }
    }
}
static HealthCheck check_hardcoded(Repo& r) {
    HealthCheck c;
    c.name = "hardcoded paths";
    const std::string pat = "/home/" + fs::path(home()).filename().string() + "/";
    std::vector<std::string> found;
    scan_hardcoded(fs::path(r.path), 1, pat, found);
    r.hardcoded_paths = found;
    c.pass = found.empty();
    c.detail = c.pass ? "none" : std::to_string(found.size()) + " file(s)";
    return c;
}
static HealthCheck check_gitleaks(Repo& r) {
    HealthCheck c;
    c.name = "gitleaks";
    if (find_in_path("gitleaks").empty() || !r.is_repo) {
        r.gitleaks = "unavailable";
        c.pass = true;
        c.detail = "unavailable";
        return c;
    }
    const fs::path tmp = fs::temp_directory_path() / ("dev_atlas_gl_" + std::to_string(::getpid()));
    std::string out;
    int rc = -1;
    const bool ok = run_argv_in(
        {"gitleaks", "detect", "--no-banner", "--report-format=json", "--report-path",
         tmp.string()},
        fs::path(r.path), out, &rc);
    if (!ok || (rc != 0 && rc != 1)) {
        r.gitleaks = "error";
        c.pass = false;
        c.detail = "error";
    } else if (rc == 0) {
        r.gitleaks = "clean";
        c.pass = true;
        c.detail = "clean";
    } else {
        r.gitleaks = std::to_string(count_ruleid(tmp)) + " finding(s)";
        c.pass = false;
        c.detail = r.gitleaks;
    }
    fs::remove(tmp);
    return c;
}
static HealthCheck check_dirty(const Repo& r) {
    HealthCheck c;
    c.name = "clean tree";
    if (!r.is_repo) {
        c.pass = true;
        c.detail = "not a git repo";
        return c;
    }
    c.pass = r.git.dirty == 0 && r.git.untracked == 0;
    c.detail = c.pass ? "clean"
                      : std::to_string(r.git.dirty) + " dirty, " +
                            std::to_string(r.git.untracked) + " untracked";
    return c;
}
static HealthCheck check_up_to_date(const Repo& r) {
    HealthCheck c;
    c.name = "up to date";
    if (!r.is_repo) {
        c.pass = true;
        c.detail = "not a git repo";
        return c;
    }
    c.pass = r.git.ahead == 0;
    c.detail = c.pass ? "up to date" : std::to_string(r.git.ahead) + " ahead";
    return c;
}
void audit_repo(Repo& repo) {
    struct Item {
        HealthCheck c;
        int weight;
    };
    const Item items[] = {
        {doc_check(repo, "README.md", "README"), 15},
        {doc_check(repo, "AGENTS.md", "AGENTS"), 10},
        {doc_check(repo, ".gitignore", "gitignore"), 10},
        {check_ceiling(repo), 20},
        {check_hardcoded(repo), 15},
        {check_gitleaks(repo), 15},
        {check_dirty(repo), 10},
        {check_up_to_date(repo), 5},
    };
    int score = 0;
    for (const auto& it : items) {
        if (it.c.pass)
            score += it.weight;
        repo.checks.push_back(it.c);
    }
    repo.health_score = score;
    repo.lifecycle.clear();
    if (repo.bucket == "labs") {
        if (repo.last_activity_days > 30) {
            repo.lifecycle = "archive";
        } else {
            for (const auto& c : repo.checks)
                if (!c.pass)
                    return;
            repo.lifecycle = "promote";
        }
    }
}
static std::string to_lower(std::string s) {
    for (auto& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}
static bool contains_ci(const std::string& hay, const std::string& needle) {
    if (needle.empty())
        return false;
    return to_lower(hay).find(to_lower(needle)) != std::string::npos;
}
static std::vector<int> parse_host_ports(const std::string& field) {
    std::vector<int> ports;
    std::stringstream ss(field);
    std::string seg;
    while (std::getline(ss, seg, ',')) {
        const size_t arrow = seg.find("->");
        if (arrow == std::string::npos)
            continue;
        const std::string left = seg.substr(0, arrow);
        const size_t colon = left.rfind(':');
        std::string digits;
        for (char ch : (colon == std::string::npos ? left : left.substr(colon + 1))) {
            if (std::isdigit(static_cast<unsigned char>(ch)))
                digits += ch;
            else
                break;
        }
        if (!digits.empty())
            ports.push_back(std::stoi(digits));
    }
    return ports;
}
static std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ls(line);
    std::string field;
    while (std::getline(ls, field, '\t'))
        parts.push_back(field);
    return parts;
}
static void attach_container(ScanResult& result, const std::string& name,
                             const std::string& image, const std::vector<int>& hports) {
    for (auto& repo : result.repos) {
        const std::string base = fs::path(repo.path).filename().string();
        if (!contains_ci(name, base))
            continue;
        repo.containers.push_back({name, image, hports.empty() ? 0 : hports.front()});
        for (int hp : hports)
            if (std::find(repo.ports.begin(), repo.ports.end(), hp) == repo.ports.end())
                repo.ports.push_back(hp);
    }
}
void audit_all(ScanResult& result) {
    for (auto& repo : result.repos)
        audit_repo(repo);
    std::string out;
    int rc = -1;
    const bool ok = run_argv({"podman", "ps", "--format",
                              "{{.Names}}\t{{.Image}}\t{{.Ports}}"},
                             out, &rc);
    if (!ok || rc != 0 || trim(out).empty())
        return;  // no live containers
    std::stringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (trim(line).empty())
            continue;
        const std::vector<std::string> parts = split_tab(line);
        if (parts.size() < 3)
            continue;
        attach_container(result, parts[0], parts[1], parse_host_ports(parts[2]));
    }
    for (auto& repo : result.repos)
        std::sort(repo.ports.begin(), repo.ports.end());
}

}  // namespace dev_atlas
