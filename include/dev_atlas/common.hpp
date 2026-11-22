// Shared data model and utilities for dev-atlas.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dev_atlas {

using Bytes = long long;

struct GitInfo {
    bool repo = false;
    std::string branch;
    long long dirty = 0;      // changed tracked files (staged + modified)
    long long untracked = 0;
    long long ahead = 0;
    long long behind = 0;
    bool has_github_remote = false;
    long long last_commit_days = -1;  // -1: not a repo or no commits yet
};

struct BloatEntry {
    std::string path;        // absolute
    std::string display;     // ~/...
    Bytes bytes = 0;
    long long dormant_days = 0;  // days since the bloat dir last changed
};

struct TopFile {
    std::string display;
    long long lines = 0;
};

struct HealthCheck {
    std::string name;
    bool pass = false;
    std::string detail;
};

struct ContainerRef {
    std::string name;
    std::string image;
    int port = 0;  // first published host port, 0 if none
};

struct Repo {
    std::string path;    // absolute
    std::string display; // ~/...
    std::string bucket;  // active | labs | archive | other
    std::string stack = "Static";
    bool is_repo = false;
    GitInfo git;
    Bytes source_bytes = 0;
    Bytes bloat_bytes = 0;
    std::vector<BloatEntry> bloat;
    long long last_activity_days = 0;
    int health_score = 0;
    std::vector<HealthCheck> checks;
    std::vector<TopFile> top_files;  // 3 largest source files by line count
    std::vector<std::string> hardcoded_paths;  // up to 5 display paths
    std::string gitleaks;   // "clean" | "N finding(s)" | "unavailable" | "error"
    std::string lifecycle;  // "" | "promote" | "archive"
    std::vector<ContainerRef> containers;
    std::vector<int> ports;
};

struct ScanResult {
    long long scanned_at = 0;  // unix epoch seconds
    Bytes total_source_bytes = 0;
    Bytes total_bloat_bytes = 0;
    std::vector<Repo> repos;
};

// -- Utilities (src/common.cpp) -------------------------------------------
const char* home();
std::string collapse_home(const std::string& path);
std::string json_escape(const std::string& s);
std::string human_bytes(Bytes bytes);
std::string trim(const std::string& s);
std::vector<std::string> csv(const std::string& s, char sep = ',');
// Shell-free command runner: args are exec'd directly, no shell parsing,
// so paths with spaces are safe. Returns false on fork/exec failure;
// *rc receives the exit status when provided.
bool run_argv(const std::vector<std::string>& args, std::string& out, int* rc = nullptr);
// Like run_argv, but the child chdir's into cwd before exec (for tools that
// operate on the current directory, e.g. gitleaks).
bool run_argv_in(const std::vector<std::string>& args, const std::filesystem::path& cwd,
                 std::string& out, int* rc = nullptr);
// Splits a simple whitespace-separated command line and runs it via run_argv.
bool run_cmd(const std::string& line, std::string& out, int* rc = nullptr);
Bytes dir_size_bytes(const std::filesystem::path& p);
long long days_since(const std::filesystem::file_time_type& t);
std::optional<std::filesystem::file_time_type> file_mtime(const std::filesystem::path& p);
long long count_lines(const std::filesystem::path& p);
}  // namespace dev_atlas
