// dev-atlas: engineering health, git sync, and disk bloat command center.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "dev_atlas/auditor.hpp"
#include "dev_atlas/common.hpp"
#include "dev_atlas/renderer.hpp"
#include "dev_atlas/scanner.hpp"
#include "dev_atlas/server.hpp"

using namespace dev_atlas;
namespace fs = std::filesystem;

static const char kUsage[] =
    "dev_atlas: scan | doctor | reclaim | report | serve\n"
    "  scan [--json] | doctor <repo> | reclaim [--apply] [--older-than N] (dry-run default)\n"
    "  report [--out dir] | serve [--port 8090]   env: DEV_ATLAS_HOME (default $HOME/development)\n";
static void usage() { std::fputs(kUsage, stdout); }
static ScanResult full_scan() {
    const char* env = std::getenv("DEV_ATLAS_HOME");
    const fs::path root = env ? fs::absolute(env) : fs::path(home()) / "development";
    std::vector<fs::path> buckets;
    for (const char* b : {"active", "labs", "archive"})
        if (fs::is_directory(root / b))
            buckets.push_back(root / b);
    ScanResult r = scan(buckets);
    audit_all(r);
    return r;
}
static int flag_index(const std::vector<std::string>& args, const std::string& flag) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == flag)
            return static_cast<int>(i);
    return -1;
}
static int cmd_scan(const std::vector<std::string>& args) {
    const bool json = std::find(args.begin(), args.end(), std::string("--json")) != args.end();
    std::cout << (json ? render_json(full_scan()) : render_table(full_scan())) << "\n";
    return 0;
}
static int cmd_doctor(const std::vector<std::string>& args) {
    Repo repo;
    repo.path = fs::absolute(args[0]).string();
    repo.display = collapse_home(repo.path);
    scan_repo(repo);
    audit_repo(repo);
    std::cout << render_doctor(repo) << "\n";
    return 0;
}
static int cmd_reclaim(const std::vector<std::string>& args) {
    long long days = 30;
    if (const int i = flag_index(args, "--older-than"); i >= 0)
        days = std::stoll(args[i + 1]);
    const bool apply = std::find(args.begin(), args.end(), std::string("--apply")) != args.end();
    const ReclaimPlan plan = plan_reclaim(full_scan(), days);
    std::cout << render_reclaim(plan, !apply) << "\n";
    if (apply)
        apply_reclaim(plan);
    return 0;
}
static int cmd_report(const std::vector<std::string>& args) {
    fs::path out = "out";
    if (const int i = flag_index(args, "--out"); i >= 0)
        out = fs::absolute(args[i + 1]);
    write_report(full_scan(), out);
    std::printf("wrote %s\n", (out / "latest.html").c_str());
    return 0;
}
static int cmd_serve(const std::vector<std::string>& args) {
    int port = 8090;
    if (const int i = flag_index(args, "--port"); i >= 0)
        port = std::stoi(args[i + 1]);
    const char* henv = std::getenv("DEV_ATLAS_HOST");
    const std::string host = henv ? henv : "127.0.0.1";
    const fs::path out = "out";
    write_report(full_scan(), out);
    std::printf("serving http://%s:%d (Ctrl-C to stop)\n", host.c_str(), port);
    return serve(out / "latest.html", out / "scan.json", port, host);
}
struct Cmd { const char* name; int (*fn)(const std::vector<std::string>&); };
static const Cmd kCmds[] = {{"scan", cmd_scan}, {"doctor", cmd_doctor}, {"reclaim", cmd_reclaim},
                            {"report", cmd_report}, {"serve", cmd_serve}};
int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::vector<std::string> args(argv + 2, argv + argc);
    for (const auto& c : kCmds)
        if (std::string(c.name) == argv[1] && !(std::string(c.name) == "doctor" && args.empty()))
            return c.fn(args);
    usage();
    return 2;
}
