// Shared test fixture: shell helpers plus a throwaway workspace of sample repos.
#pragma once
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "dev_atlas/common.hpp"

using namespace dev_atlas;
namespace fxfs = std::filesystem;

inline std::string sh(const std::string& cmd) {
    std::string out;
    run_cmd(cmd, out);
    return out;
}
inline void shc(const std::string& cmd) {
    std::string o;
    run_argv({"sh", "-c", cmd}, o);
}
inline std::string iso_days_ago(long long days) {
    auto t = std::chrono::system_clock::now() - std::chrono::hours(days * 24);
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::gmtime(&tt));
    return buf;
}
inline void git_init(const fxfs::path& p) {
    sh("git init " + p.string());
    sh("git -C " + p.string() + " config user.email t@t");
    sh("git -C " + p.string() + " config user.name t");
}
inline void git_commit(const fxfs::path& p, const std::string& msg) {
    sh("git -C " + p.string() + " add -A");
    sh("git -C " + p.string() + " commit -qm " + msg);
}
inline void wf(const fxfs::path& p, const std::string& content) {
    fxfs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
}
inline void wf_bytes(const fxfs::path& p, size_t bytes) {
    fxfs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(std::string(bytes, 'x').c_str(), static_cast<std::streamsize>(bytes));
}

// Nine sample repos: dirty python repo, nested node repo, cold repo with bloat,
// warm (all docs) repo, over-ceiling repo, hardcoded-path repo (docs exempt),
// gitleaks-bait repo, plain static dir, and a repo with an old commit but a
// freshly touched file (regression: activity must use the most recent event).
inline fxfs::path build_fixture() {
    auto ws = fxfs::path(home()) / (".dt_ws_" + std::to_string(::getpid()));
    const auto a = ws / "active";
    const auto l = ws / "labs";
    const auto ar = ws / "archive";
    auto py = a / "proj-py";
    git_init(py);
    wf(py / "pyproject.toml", "[project]\nname='x'\n");
    wf(py / "src/a.py", "print('hi')\n");
    git_commit(py, "init");
    wf(py / "src/a.py", "print('hi')\nprint('more')\n");
    wf(py / "src/b.py", "print('b')\n");
    wf_bytes(py / "node_modules/f.bin", 5 * 1024 * 1024);

    auto node = a / "group" / "proj-node";
    git_init(node);
    wf(node / "package.json", "{\"name\":\"x\"}\n");
    git_commit(node, "init");

    auto cold = l / "proj-cold";
    git_init(cold);
    wf(cold / "README.md", "# cold\n");
    shc("cd " + cold.string() + " && git add -A && GIT_AUTHOR_DATE='" + iso_days_ago(40) +
        "' GIT_COMMITTER_DATE='" + iso_days_ago(40) + "' git commit -qm init");
    shc("touch -d '40 days ago' " + (cold / "README.md").string());
    wf_bytes(cold / "node_modules/f.bin", 2 * 1024 * 1024);
    shc("touch -d '40 days ago' " + (cold / "node_modules/f.bin").string());

    auto warm = l / "proj-warm";
    git_init(warm);
    wf(warm / "README.md", "# warm\n");
    wf(warm / "AGENTS.md", "# agents\n");
    wf(warm / ".gitignore", "out/\n");
    wf(warm / "src/main.py", "print(1)\n");
    git_commit(warm, "init");

    auto big = l / "proj-big";
    git_init(big);
    wf(big / "README.md", "# big\n");
    wf(big / "AGENTS.md", "# agents\n");
    wf(big / ".gitignore", "out/\n");
    std::string bigfile;
    for (int i = 0; i < 400; ++i)
        bigfile += "line " + std::to_string(i) + "\n";
    wf(big / "src/big.py", bigfile);
    git_commit(big, "init");

    auto hard = l / "proj-hard";
    git_init(hard);
    const std::string hp = "/home/" + fxfs::path(home()).filename().string() + "/";
    wf(hard / "README.md", "# hard\n\nExample path: " + hp + "data\n");
    wf(hard / "compose.override.yml", "services:\n  x:\n    volumes: [\"" + hp + "data:/data\"]\n");
    wf(hard / "src/conf.py", "path = \"" + hp + "secret\"\n");
    git_commit(hard, "init");

    auto leak = l / "proj-leak";
    git_init(leak);
    wf(leak / "README.md", "# leak\n");
    const char* key = "AKIA"
                      "Q7X9M2P4W8N3R6T1";
    const char* sec = "wJ9rK2mX5pL8nQ1vB4tY7zA3dF6hC0sG9wE2uI5o";
    wf(leak / "src/creds.py", std::string("key = \"") + key + "\"\nsecret = \"" + sec + "\"\n");
    git_commit(leak, "init");

    auto fresh = l / "proj-fresh";
    git_init(fresh);
    wf(fresh / "src/main.py", "print(1)\n");
    shc("cd " + fresh.string() + " && git add -A && GIT_AUTHOR_DATE='" + iso_days_ago(45) +
        "' GIT_COMMITTER_DATE='" + iso_days_ago(45) + "' git commit -qm init");
    wf_bytes(fresh / "node_modules/f.bin", 1024 * 1024);
    shc("touch " + (fresh / "src/main.py").string());

    auto static1 = ar / "static1";
    wf(static1 / "index.html", "<html></html>\n");
    return ws;
}
