// Workspace discovery, git status, disk split, and reclaim planning.
#include "dev_atlas/scanner.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <future>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace dev_atlas {

namespace {

const std::vector<std::string>& bloat_list() {
    static const std::vector<std::string> names = {
        ".venv", "venv", "node_modules", "build", "target", "out", ".ruff_cache", ".pytest_cache"};
    return names;
}

bool is_bloat(const std::string& name) {
    for (const auto& n : bloat_list())
        if (name == n)
            return true;
    return name.rfind("venv", 0) == 0;  // prefix match: venv, venv2, ...
}

bool has_git(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / ".git", ec);  // non-throwing: perm-denied subtree
}

// Recursively collect git repo dirs (dirs holding .git), skipping hidden,
// bloat, and .worktrees dirs. Nested repos inside a repo are kept.
void walk_repos(const fs::path& dir, std::vector<fs::path>& out) {
    std::error_code ec;
    if (has_git(dir))
        out.push_back(dir);
    for (const auto& e :
             fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code ec2;
        if (!e.is_directory(ec2))
            continue;
        const std::string name = e.path().filename().string();
        if (!name.empty() && name[0] == '.')
            continue;  // hidden: .git, .worktrees, .venv, ...
        if (is_bloat(name))
            continue;
        walk_repos(e.path(), out);
    }
}

// File extensions present within three levels, skipping .git and bloat dirs.
void collect_exts(const fs::path& dir, int depth, std::set<std::string>& seen) {
    if (depth > 3)
        return;
    std::error_code ec;
    for (const auto& e :
             fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code ec2;
        const fs::path p = e.path();
        if (e.is_directory(ec2)) {
            const std::string name = p.filename().string();
            if (name == ".git" || is_bloat(name))
                continue;
            collect_exts(p, depth + 1, seen);
        } else if (e.is_regular_file(ec2)) {
            const std::string ext = p.extension().string();
            if (!ext.empty())
                seen.insert(ext);
        }
    }
}

std::string detect_stack(const fs::path& root) {
    const auto top = [&](const std::string& n) {
        std::error_code ec;
        return fs::exists(root / n, ec);
    };
    const auto top_prefix = [&](const std::string& prefix) {
        std::error_code ec;
        for (const auto& e :
             fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
            if (e.is_regular_file(ec) && e.path().filename().string().starts_with(prefix))
                return true;
        return false;
    };
    std::set<std::string> exts;
    collect_exts(root, 1, exts);
    const auto has = [&](const std::initializer_list<const char*> list) {
        for (const auto* e : list)
            if (exts.count(std::string(e)))
                return true;
        return false;
    };
    if (top("project.godot"))
        return "Godot";
    if (top("CMakeLists.txt") || has({".cpp", ".cc", ".cxx", ".h", ".hpp", ".ino"}))
        return "C++";
    if (top("pyproject.toml") || top("setup.py") || top("requirements.txt") || has({".py"}))
        return "Python";
    if (top("package.json"))
        return "Node";
    if (has({".scad"}))
        return "CAD";
    if (top_prefix("Dockerfile") || top("docker-compose.yml") || top("compose.yml") ||
        top("compose.yaml"))
        return "Container";
    return "Static";
}

struct SizeAccum {
    Bytes total = 0;
    Bytes bloat = 0;
    std::vector<BloatEntry> entries;
    long long newest = 0;
    bool have = false;
};

// Newest file mtime within a dir. The dir's own mtime changes on every entry
// add/remove, so it says nothing about content age.
std::optional<fs::file_time_type> newest_mtime_in(const fs::path& dir) {
    std::optional<fs::file_time_type> newest;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;
        auto mt = it->last_write_time(ec);
        if (ec)
            continue;
        if (!newest || mt < *newest)
            newest = mt;
    }
    return newest;
}

void walk_size(const fs::path& dir, SizeAccum& acc) {
    std::error_code ec;
    for (const auto& e :
             fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code ec2;
        const fs::path p = e.path();
        const std::string name = p.filename().string();
        if (e.is_directory(ec2)) {
            if (name == ".git" || has_git(p))
                continue;  // VCS metadata / nested repo: not counted here
            if (is_bloat(name)) {
                const Bytes sz = dir_size_bytes(p);
                acc.total += sz;
                acc.bloat += sz;
                BloatEntry be;
                be.path = p.string();
                be.display = collapse_home(p.string());
                be.bytes = sz;
                if (auto mt = newest_mtime_in(p))
                    be.dormant_days = days_since(*mt);
                acc.entries.push_back(std::move(be));
                continue;
            }
            walk_size(p, acc);
        } else if (e.is_regular_file(ec2)) {
            acc.total += static_cast<Bytes>(e.file_size(ec2));
            if (auto mt = file_mtime(p)) {
                const long long d = days_since(*mt);
                if (!acc.have || d < acc.newest) {
                    acc.newest = d;
                    acc.have = true;
                }
            }
        }
    }
}

void git_cmd(const std::string& path, const std::vector<std::string>& suffix, std::string& out,
             int& rc) {
    std::vector<std::string> args = {"git", "-C", path};
    args.insert(args.end(), suffix.begin(), suffix.end());
    rc = 0;
    run_argv(args, out, &rc);
}

void fill_git(Repo& repo) {
    if (!repo.is_repo)
        return;
    const std::string& p = repo.path;
    repo.git.repo = true;
    std::string out;
    int rc = 0;

    git_cmd(p, {"branch", "--show-current"}, out, rc);
    repo.git.branch = trim(out);

    git_cmd(p, {"status", "--porcelain=v1"}, out, rc);
    long long dirty = 0, untracked = 0;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty())
            continue;
        if (line.rfind("??", 0) == 0)
            ++untracked;
        else
            ++dirty;
    }
    repo.git.dirty = dirty;
    repo.git.untracked = untracked;

    git_cmd(p, {"rev-list", "--left-right", "--count", "HEAD...@{u}"}, out, rc);
    if (rc == 0) {
        const size_t tab = out.find('\t');
        if (tab != std::string::npos) {
            repo.git.ahead = std::stoll(trim(out.substr(0, tab)));
            repo.git.behind = std::stoll(trim(out.substr(tab + 1)));
        }
    }

    git_cmd(p, {"remote", "-v"}, out, rc);
    repo.git.has_github_remote = out.find("github.com") != std::string::npos;

    git_cmd(p, {"log", "-1", "--format=%ct"}, out, rc);
    if (rc == 0) {
        try {
            const long long epoch = std::stoll(trim(out));
            repo.git.last_commit_days = (time(nullptr) - epoch) / 86400;
        } catch (...) {
            repo.git.last_commit_days = -1;
        }
    } else {
        repo.git.last_commit_days = -1;
    }
}

void fill_sizes(Repo& repo) {
    SizeAccum acc;
    walk_size(fs::path(repo.path), acc);
    repo.source_bytes = acc.total - acc.bloat;
    repo.bloat_bytes = acc.bloat;
    repo.bloat = std::move(acc.entries);
    const long long mtime_days = acc.have ? acc.newest : 0;
    long long act = std::max(0LL, mtime_days);
    if (repo.git.last_commit_days >= 0)
        act = std::min(act, repo.git.last_commit_days);
    repo.last_activity_days = act;
}

std::string bucket_for(const fs::path& p, const std::vector<fs::path>& roots) {
    const std::string ps = fs::absolute(p).string();
    for (const auto& r : roots) {
        const fs::path ar = fs::absolute(r);
        if (ps.starts_with(ar.string() + "/")) {
            const std::string b = ar.filename().string();
            return b.empty() ? "other" : b;
        }
    }
    return "other";
}

}  // namespace

const std::vector<std::string>& bloat_names() {
    return bloat_list();
}

bool is_bloat_name(const std::string& name) {
    return is_bloat(name);
}

std::vector<fs::path> discover_projects(const std::vector<fs::path>& roots) {
    std::vector<fs::path> found;
    for (const auto& root : roots) {
        std::error_code ec0;
        if (!fs::is_directory(root, ec0))
            continue;
        walk_repos(root, found);
        std::error_code ec;
        for (const auto& e :
             fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code ec2;
            if (!e.is_directory(ec2))
                continue;
            const std::string name = e.path().filename().string();
            if (!name.empty() && name[0] == '.')
                continue;
            if (is_bloat(name))
                continue;
            if (has_git(e.path()))
                continue;  // already counted as a repo
            std::vector<fs::path> sub;
            walk_repos(e.path(), sub);
            if (sub.empty())
                found.push_back(e.path());
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

void scan_repo(Repo& repo) {
    const fs::path root(repo.path);
    repo.is_repo = has_git(root);
    repo.stack = detect_stack(root);
    fill_git(repo);
    fill_sizes(repo);
}

ScanResult scan(const std::vector<fs::path>& roots) {
    const std::vector<fs::path> projects = discover_projects(roots);
    ScanResult result;
    std::vector<std::future<Repo>> futures;
    futures.reserve(projects.size());
    for (const auto& p : projects) {
        futures.push_back(std::async(std::launch::async, [p, roots]() {
            Repo r;
            r.path = p.string();
            r.display = collapse_home(p.string());
            r.bucket = bucket_for(p, roots);
            scan_repo(r);
            return r;
        }));
    }
    for (auto& f : futures) {
        Repo r = f.get();
        result.total_source_bytes += r.source_bytes;
        result.total_bloat_bytes += r.bloat_bytes;
        result.repos.push_back(std::move(r));
    }
    result.scanned_at = time(nullptr);
    return result;
}

}  // namespace dev_atlas
