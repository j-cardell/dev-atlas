// Output rendering: JSON, table, doctor, reclaim, HTML.
#include "dev_atlas/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace dev_atlas {

namespace {

void append_git(std::string& s, const GitInfo& g) {
    s += ",\"git\":{\"branch\":\"" + json_escape(g.branch) + "\""
         + ",\"dirty\":" + std::to_string(g.dirty)
         + ",\"untracked\":" + std::to_string(g.untracked)
         + ",\"ahead\":" + std::to_string(g.ahead)
         + ",\"behind\":" + std::to_string(g.behind)
         + ",\"has_github_remote\":" + std::string(g.has_github_remote ? "true" : "false")
         + ",\"last_commit_days\":" + std::to_string(g.last_commit_days) + "}";
}

void append_bloat(std::string& s, const std::vector<BloatEntry>& v) {
    s += ",\"bloat\":[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "{\"path\":\"" + json_escape(v[i].display) + "\",\"bytes\":"
             + std::to_string(v[i].bytes) + ",\"dormant_days\":"
             + std::to_string(v[i].dormant_days) + "}";
    }
    s += "]";
}

void append_checks(std::string& s, const std::vector<HealthCheck>& v) {
    s += ",\"checks\":[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "{\"name\":\"" + json_escape(v[i].name) + "\""
             + ",\"pass\":" + std::string(v[i].pass ? "true" : "false")
             + ",\"detail\":\"" + json_escape(v[i].detail) + "\"}";
    }
    s += "]";
}

void append_containers(std::string& s, const std::vector<ContainerRef>& v) {
    s += ",\"containers\":[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "{\"name\":\"" + json_escape(v[i].name) + "\""
             + ",\"image\":\"" + json_escape(v[i].image) + "\""
             + ",\"port\":" + std::to_string(v[i].port) + "}";
    }
    s += "]";
}

void append_str_arr(std::string& s, const char* key, const std::vector<std::string>& v) {
    s += ",\"" + std::string(key) + "\":";
    s += "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += "\"" + json_escape(v[i]) + "\"";
    }
    s += "]";
}

void append_int_arr(std::string& s, const char* key, const std::vector<int>& v) {
    s += ",\"" + std::string(key) + "\":";
    s += "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    s += "]";
}

std::string json_repo(const Repo& r) {
    std::string s;
    s += "{\"path\":\"" + json_escape(r.display) + "\""
         + ",\"display\":\"" + json_escape(r.display) + "\""
         + ",\"bucket\":\"" + json_escape(r.bucket) + "\""
         + ",\"stack\":\"" + json_escape(r.stack) + "\""
         + ",\"is_repo\":" + std::string(r.is_repo ? "true" : "false");
    append_git(s, r.git);
    s += ",\"source_bytes\":" + std::to_string(r.source_bytes)
         + ",\"bloat_bytes\":" + std::to_string(r.bloat_bytes);
    append_bloat(s, r.bloat);
    s += ",\"last_activity_days\":" + std::to_string(r.last_activity_days)
         + ",\"health_score\":" + std::to_string(r.health_score);
    append_checks(s, r.checks);
    s += ",\"top_files\":";
    s += "[";
    for (size_t i = 0; i < r.top_files.size(); ++i) {
        if (i)
            s += ",";
        s += "{\"path\":\"" + json_escape(r.top_files[i].display) +
             "\",\"lines\":" + std::to_string(r.top_files[i].lines) + "}";
    }
    s += "]";
    append_str_arr(s, "hardcoded_paths", r.hardcoded_paths);
    s += ",\"gitleaks\":\"" + json_escape(r.gitleaks) + "\""
         + ",\"lifecycle\":\"" + json_escape(r.lifecycle) + "\"";
    append_containers(s, r.containers);
    append_int_arr(s, "ports", r.ports);
    s += "}";
    return s;
}

std::string table_notes(const Repo& r) {
    std::string n;
    if (!r.lifecycle.empty()) n += r.lifecycle;
    if (r.health_score < 60) { if (!n.empty()) n += " "; n += "needs attention"; }
    if (r.git.dirty + r.git.untracked > 0) { if (!n.empty()) n += " "; n += "dirty"; }
    if (r.git.ahead > 0) { if (!n.empty()) n += " "; n += "unpushed"; }
    if (r.bloat_bytes > 0 && r.last_activity_days > 30) { if (!n.empty()) n += " "; n += "cold bloat"; }
    return n;
}

std::string pad(const std::string& s, int w) {
    return s + std::string(std::max(0, w - static_cast<int>(s.size())), ' ');
}

}  // namespace

std::string render_json(const ScanResult& result) {
    std::string s;
    s += "{\"scanned_at\":" + std::to_string(result.scanned_at)
         + ",\"total_source_bytes\":" + std::to_string(result.total_source_bytes)
         + ",\"total_bloat_bytes\":" + std::to_string(result.total_bloat_bytes)
         + ",\"repos\":[";
    for (size_t i = 0; i < result.repos.size(); ++i) {
        if (i) s += ",";
        s += json_repo(result.repos[i]);
    }
    s += "]}";
    return s;
}

std::string render_table(const ScanResult& result) {
    std::vector<std::array<std::string, 9>> rows;
    for (const auto& r : result.repos) {
        auto base = r.display;
        auto slash = base.find_last_of('/');
        char gitbuf[128] = {};
        std::snprintf(gitbuf, sizeof gitbuf, "%s d:%lld u:%lld + %lld",
                      r.git.branch.c_str(), r.git.dirty, r.git.untracked, r.git.ahead);
        rows.push_back({
            slash != std::string::npos ? base.substr(slash + 1) : base,
            r.bucket, r.stack, std::to_string(r.health_score), gitbuf,
            human_bytes(r.source_bytes), human_bytes(r.bloat_bytes),
            std::to_string(r.last_activity_days) + "d", table_notes(r)});
    }
    int w[9] = {};
    for (int c = 0; c < 9; ++c) {
        int m = 0;
        for (const auto& row : rows) m = std::max(m, static_cast<int>(row[c].size()));
        w[c] = m;
    }
    w[0] = std::min(std::max(w[0], 4), 24);
    w[3] = std::max(w[3], 6);
    std::string out;
    const char* hdr[] = {"NAME", "BUCKET", "STACK", "HEALTH", "GIT", "SRC", "BLOAT", "LAST", "NOTES"};
    for (int c = 0; c < 9; ++c) out += pad(hdr[c], w[c]) + (c < 8 ? "  " : "\n");
    for (const auto& row : rows) {
        for (int c = 0; c < 9; ++c) out += pad(row[c], w[c]) + (c < 8 ? "  " : "\n");
    }
    return out;
}

std::string render_doctor(const Repo& repo) {
    std::string s;
    s += repo.display + "  [" + repo.bucket + "]  " + repo.stack + "\n";
    s += "health: " + std::to_string(repo.health_score) + "/100\n\n";
    s += "GIT\n";
    s += "  branch: " + (repo.git.branch.empty() ? "(none)" : repo.git.branch) + "\n";
    s += "  dirty: " + std::to_string(repo.git.dirty)
         + "  untracked: " + std::to_string(repo.git.untracked) + "\n";
    s += "  ahead: " + std::to_string(repo.git.ahead)
         + "  behind: " + std::to_string(repo.git.behind) + "\n";
    s += "  github remote: " + std::string(repo.git.has_github_remote ? "yes" : "no") + "\n";
    s += "  last commit: " + std::to_string(repo.git.last_commit_days) + "d\n\n";
    s += "SIZES\n";
    s += "  source: " + human_bytes(repo.source_bytes) + "\n";
    s += "  bloat:  " + human_bytes(repo.bloat_bytes) + "\n\n";
    s += "CHECKS\n";
    for (const auto& c : repo.checks)
        s += "  " + std::string(c.pass ? "PASS " : "FAIL ") + c.name + ": " + c.detail + "\n";
    if (!repo.checks.empty()) s += "\n";
    s += "TOP FILES\n";
    if (repo.top_files.empty()) {
        s += "  (none)\n";
    } else {
        for (const auto& f : repo.top_files)
            s += "  " + f.display + "  " + std::to_string(f.lines) + " lines\n";
    }
    s += "\n";
    s += "HARDCODED PATHS\n";
    if (repo.hardcoded_paths.empty()) {
        s += "  (none)\n";
    } else {
        for (const auto& p : repo.hardcoded_paths)
            s += "  " + p + "\n";
    }
    s += "\nGITLEAKS\n  " + repo.gitleaks + "\n\n";
    s += "CONTAINERS\n";
    if (repo.containers.empty()) {
        s += "  (none)\n";
    } else {
        for (const auto& c : repo.containers)
            s += "  " + c.name + "  " + c.image + "  :" + std::to_string(c.port) + "\n";
    }
    s += "PORTS\n";
    if (repo.ports.empty()) {
        s += "  (none)\n";
    } else {
        for (size_t i = 0; i < repo.ports.size(); ++i)
            s += "  " + std::to_string(repo.ports[i]) + "\n";
    }
    s += "\nLIFECYCLE\n  " + (repo.lifecycle.empty() ? std::string("none") : repo.lifecycle) + "\n";
    return s;
}

std::string render_reclaim(const ReclaimPlan& plan, bool dry_run) {
    std::string s;
    for (const auto& e : plan.entries)
        s += e.display + "  " + human_bytes(e.bytes) + "  dormant "
             + std::to_string(e.dormant_days) + "d\n";
    s += "TOTAL  " + human_bytes(plan.total) + "\n";
    if (dry_run)
        s += "dry-run: nothing deleted (re-run with --apply to delete)\n";
    return s;
}

std::string render_html(const ScanResult& result, const std::filesystem::path& template_path) {
    std::ifstream f(template_path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string tpl = ss.str();
    auto pos = tpl.find("__DATA__");
    if (pos == std::string::npos) return tpl;
    const std::string json = render_json(result);
    std::string safe;
    safe.reserve(json.size() + 16);
    for (size_t i = 0; i < json.size(); ++i) {
        safe += json[i];
        if (json[i] == '<' && i + 1 < json.size() && json[i + 1] == '/')
            safe += '\\';
    }
    tpl.replace(pos, 8, safe);
    return tpl;
}

std::filesystem::path find_template() {
    if (const char* dir = std::getenv("DEV_ATLAS_TEMPLATES")) {
        auto p = std::filesystem::path(dir) / "dashboard.html";
        if (std::filesystem::exists(p)) return p;
    }
    char buf[4096] = {};
    if (readlink("/proc/self/exe", buf, sizeof buf - 1) > 0) {
        auto exe = std::filesystem::path(buf).parent_path() / "templates" / "dashboard.html";
        if (std::filesystem::exists(exe)) return exe;
    }
    auto local = std::filesystem::path("templates/dashboard.html");
    if (std::filesystem::exists(local)) return local;
    auto opt = std::filesystem::path("/opt/dev-atlas/templates/dashboard.html");
    if (std::filesystem::exists(opt)) return opt;
    return {};
}

void write_report(const ScanResult& result, const std::filesystem::path& out_dir) {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    auto tpl = find_template();
    std::string html;
    if (!tpl.empty()) {
        html = render_html(result, tpl);
    } else {
        html = "<html><body><h1>dev-atlas</h1><p>template missing</p></body></html>\n";
    }
    {
        std::ofstream f(out_dir / "latest.html");
        f << html;
    }
    {
        std::ofstream f(out_dir / "scan.json");
        f << render_json(result);
    }
}

}  // namespace dev_atlas
