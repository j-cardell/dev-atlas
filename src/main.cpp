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

static void usage() {
    std::printf(
        "dev_atlas <command>\n"
        "  scan [--json]                              table or JSON workspace scan\n"
        "  doctor <repo>                               detailed audit of one repo\n"
        "  reclaim [--dry-run] [--apply] [--older-than <days>]\n"
        "                                             plan dormant bloat (dry-run is the default;\n"
        "                                             --apply deletes the planned bloat dirs)\n"
        "  report [--out <dir>]                        write <dir>/latest.html (default out/)\n"
        "  serve [--port 8090]                         dashboard + /api/scan.json on 127.0.0.1\n"
        "env: DEV_ATLAS_HOME  workspace root (default $HOME/development)\n");
}

static fs::path workspace_root() {
    if (const char* e = std::getenv("DEV_ATLAS_HOME"))
        return fs::absolute(e);
    return fs::path(home()) / "development";
}

static ScanResult full_scan() {
    const fs::path root = workspace_root();
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

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);

    if (cmd == "scan") {
        const bool json = std::find(args.begin(), args.end(), std::string("--json")) != args.end();
        std::cout << (json ? render_json(full_scan()) : render_table(full_scan())) << "\n";
        return 0;
    }
    if (cmd == "doctor" && args.size() >= 1) {
        Repo repo;
        repo.path = fs::absolute(args[0]).string();
        repo.display = collapse_home(repo.path);
        scan_repo(repo);
        audit_repo(repo);
        std::cout << render_doctor(repo) << "\n";
        return 0;
    }
    if (cmd == "reclaim") {
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
    if (cmd == "report") {
        fs::path out = "out";
        if (const int i = flag_index(args, "--out"); i >= 0)
            out = fs::absolute(args[i + 1]);
        write_report(full_scan(), out);
        std::printf("wrote %s\n", (out / "latest.html").c_str());
        return 0;
    }
    if (cmd == "serve") {
        int port = 8090;
        if (const int i = flag_index(args, "--port"); i >= 0)
            port = std::stoi(args[i + 1]);
        const fs::path out = "out";
        write_report(full_scan(), out);
        std::printf("serving http://127.0.0.1:%d (Ctrl-C to stop)\n", port);
        return serve(out / "latest.html", out / "scan.json", port);
    }
    usage();
    return 2;
}
