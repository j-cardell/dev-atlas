// dev-atlas test suite: plain stdlib harness, no framework.
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>

#include "dev_atlas/auditor.hpp"
#include "dev_atlas/common.hpp"
#include "dev_atlas/renderer.hpp"
#include "dev_atlas/scanner.hpp"
#include "dev_atlas/server.hpp"
#include "json_lint.hpp"

namespace fs = std::filesystem;
using namespace dev_atlas;

static int failures = 0;
static void check(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++failures; }
}

static std::string sh(const std::string& cmd) {
    std::string out;
    run_cmd(cmd, out);
    return out;
}
static void shc(const std::string& cmd) {
    std::string o;
    run_argv({"sh", "-c", cmd}, o);
}

static std::string iso_days_ago(long long days) {
    auto t = std::chrono::system_clock::now() - std::chrono::hours(days * 24);
    std::time_t tt = std::chrono::system_clock::to_time_t(t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::gmtime(&tt));
    return buf;
}
static void git_init(const fs::path& p) {
    sh("git init " + p.string());
    sh("git -C " + p.string() + " config user.email t@t");
    sh("git -C " + p.string() + " config user.name t");
}
static void git_commit(const fs::path& p, const std::string& msg) {
    sh("git -C " + p.string() + " add -A");
    sh("git -C " + p.string() + " commit -qm " + msg);
}
static void wf(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
}
static void wf_bytes(const fs::path& p, size_t bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(std::string(bytes, 'x').c_str(), static_cast<std::streamsize>(bytes));
}

static fs::path build_fixture() {
    auto ws = fs::path(home()) / (".dt_ws_" + std::to_string(::getpid()));
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
    for (int i = 0; i < 400; ++i) bigfile += "line " + std::to_string(i) + "\n";
    wf(big / "src/big.py", bigfile);
    git_commit(big, "init");

    auto hard = l / "proj-hard";
    git_init(hard);
    wf(hard / "README.md", "# hard\n");
    wf(hard / "src/conf.py", "path = \"/home/jon/secret\"\n");
    git_commit(hard, "init");

    auto leak = l / "proj-leak";
    git_init(leak);
    wf(leak / "README.md", "# leak\n");
    const char* key = "AKIA" "Q7X9M2P4W8N3R6T1";
    const char* sec = "wJ9rK2mX5pL8nQ1vB4tY7zA3dF6hC0sG9wE2uI5o";
    wf(leak / "src/creds.py", std::string("key = \"") + key + "\"\nsecret = \"" + sec + "\"\n");
    git_commit(leak, "init");

    auto static1 = ar / "static1";
    wf(static1 / "index.html", "<html></html>\n");
    return ws;
}

static void test_collapse() {
    check(std::string(collapse_home(home())) == "~", "collapse home");
    check(collapse_home(std::string(home()) + "/x/y") == "~/x/y", "collapse home/x/y");
    check(collapse_home("/etc/passwd") == "/etc/passwd", "collapse /etc unchanged");
}

static void test_json_escape() {
    check(json_escape("a\"b\\c") == "a\\\"b\\\\c", "json_escape quotes");
    check(json_escape("a\nb") == "a\\nb", "json_escape newline");
    check(json_escape(std::string(1, '\x01')) == "\\u0001", "json_escape 0x01");
}

static void test_human_bytes() {
    check(human_bytes(0) == "0 B", "human 0");
    check(human_bytes(1023) == "1023 B", "human 1023");
    check(human_bytes(1024) == "1.0 KiB", "human 1024");
    check(human_bytes(1572864) == "1.5 MiB", "human 1.5 MiB");
    check(human_bytes(1073741824LL) == "1.0 GiB", "human 1 GiB");
}

static void test_count_lines(const fs::path& ws) {
    auto p = ws / "cl_test.txt";
    wf(p, "a\nb\n");
    check(count_lines(p) == 2, "count_lines 2");
    wf(p, "abc");
    check(count_lines(p) == 1, "count_lines 1");
    wf(p, "");
    check(count_lines(p) == 0, "count_lines 0");
}

static void test_dir_size(const fs::path& ws) {
    auto d = ws / "sizedir";
    wf(d / "a", std::string(100, 'x'));
    wf(d / "b", std::string(200, 'y'));
    wf(d / "c", std::string(300, 'z'));
    check(dir_size_bytes(d) == 600, "dir_size 600");
}

static void test_days_since(const fs::path& ws) {
    auto p = ws / "ds_test.txt";
    wf(p, "now");
    check(days_since(fs::last_write_time(p)) == 0, "days_since now=0");
    shc("touch -d '40 days ago' " + p.string());
    auto d = days_since(fs::last_write_time(p));
    check(d >= 39 && d <= 41, "days_since 40d");
}

static void test_discover(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto projects = discover_projects(roots);
    check(projects.size() == 8, "discover 8 projects");
}

static void test_scan(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    check(result.repos.size() == 8, "scan 8 repos");
    for (const auto& r : result.repos) {
        if (r.display.find("proj-py") != std::string::npos) {
            check(r.stack == "Python", "proj-py stack Python");
            check(!r.git.branch.empty(), "proj-py branch nonempty");
            check(r.git.last_commit_days >= 0, "proj-py last_commit >= 0");
            check(r.git.dirty == 1, "proj-py dirty 1");
            check(r.git.untracked >= 1, "proj-py untracked >=1");
            check(r.bloat_bytes > 0, "proj-py bloat > 0");
        }
        if (r.display.find("proj-node") != std::string::npos)
            check(r.stack == "Node", "proj-node stack Node");
        if (r.display.find("static1") != std::string::npos)
            check(r.stack == "Static", "static1 stack Static");
    }
    Bytes ts = 0, tb = 0;
    for (const auto& r : result.repos) { ts += r.source_bytes; tb += r.bloat_bytes; }
    check(result.total_source_bytes == ts, "total source == sum");
    check(result.total_bloat_bytes == tb, "total bloat == sum");
}

static void test_reclaim(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    auto plan = plan_reclaim(result, 30);
    for (const auto& e : plan.entries) {
        check(e.display.find("proj-cold") != std::string::npos, "reclaim from proj-cold");
        check(e.path.find("/src/") == std::string::npos, "reclaim not in src");
    }
    Bytes cold_bloat = 0;
    for (const auto& r : result.repos)
        if (r.display.find("proj-cold") != std::string::npos) cold_bloat = r.bloat_bytes;
    check(plan.total == cold_bloat, "reclaim total == cold bloat");
    const auto ws2 = ws.parent_path() / (ws.filename().string() + "_copy");
    fs::copy(ws, ws2, fs::copy_options::recursive);
    auto plan2 = plan_reclaim(scan({ws2 / "active", ws2 / "labs", ws2 / "archive"}), 30);
    apply_reclaim(plan2);
    check(!fs::exists(ws2 / "labs" / "proj-cold" / "node_modules"), "apply removed node_modules");
    check(fs::exists(ws2 / "labs" / "proj-cold" / "README.md"), "apply kept source");
    fs::remove_all(ws2);
}

static void test_auditor(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    audit_all(result);
    for (const auto& r : result.repos) {
        if (r.display.find("proj-warm") != std::string::npos) {
            check(r.health_score == 100, "warm score 100");
            check(r.lifecycle == "promote", "warm lifecycle promote");
        }
        if (r.display.find("proj-big") != std::string::npos) {
            check(r.health_score == 80, "big score 80");
            check(!r.top_files.empty() && r.top_files[0].lines == 400, "big top file 400 lines");
        }
        if (r.display.find("proj-cold") != std::string::npos)
            check(r.lifecycle == "archive", "cold lifecycle archive");
        if (r.display.find("proj-hard") != std::string::npos)
            check(!r.hardcoded_paths.empty(), "hard has hardcoded paths");
        if (r.display.find("proj-leak") != std::string::npos) {
            check(r.gitleaks == "unavailable" ||
                      r.gitleaks.find("finding(s)") != std::string::npos,
                  "leak gitleaks finding or unavailable");
        }
    }
}

static void test_render_json(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    auto j = render_json(result);
    check(!j.empty(), "json nonempty");
    check(j.find("\"repos\"") != std::string::npos, "json has repos");
    check(j.find("~") != std::string::npos, "json has ~");
    check(j.find(std::string(home())) == std::string::npos, "json no absolute home");
    check(json_lint(j), "json structurally valid");
}

static void test_render_table(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    auto t = render_table(result);
    check(t.find("proj-warm") != std::string::npos &&
          t.find(std::string(home())) == std::string::npos, "table: repo present, no absolute home");
}

static void test_render_html(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    fs::path tpl;
    for (const auto& p : {fs::path("templates/dashboard.html"), fs::path("../templates/dashboard.html")})
        if (fs::exists(p)) { tpl = p; break; }
    check(!tpl.empty(), "template found");
    auto h = render_html(result, tpl);
    check(h.find("const DATA = {") != std::string::npos, "html has DATA");
    check(h.find("proj-warm") != std::string::npos, "html has proj-warm");
    check(h.find("__DATA__") == std::string::npos, "html no __DATA__");
    check(h.find(std::string(home())) == std::string::npos, "html no absolute home");
}

static int free_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr);
    socklen_t len = sizeof addr;
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

static std::string http_get(int port, const std::string& path) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof addr);
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ::send(fd, req.data(), req.size(), 0);
    std::string resp;
    char buf[4096];
    ssize_t n;
    while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0)
        resp.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return resp;
}

static void test_server(const fs::path& ws) {
    int port = free_port();
    auto html = ws / "srv.html";
    auto json = ws / "srv.json";
    wf(html, "<html>DT</html>\n");
    wf(json, "{\"repos\":[]}\n");
    std::thread(serve, html, json, port, "127.0.0.1").detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto r1 = http_get(port, "/");
    check(r1.find("DT") != std::string::npos, "server / has DT");
    auto r2 = http_get(port, "/api/scan.json");
    check(r2.find("\"repos\"") != std::string::npos, "server json has repos");
}

int main() {
    auto ws = build_fixture();
    test_collapse();
    test_json_escape();
    test_human_bytes();
    test_count_lines(ws);
    test_dir_size(ws);
    test_days_since(ws);
    test_discover(ws);
    test_scan(ws);
    test_reclaim(ws);
    test_auditor(ws);
    test_render_json(ws);
    test_render_table(ws);
    test_render_html(ws);
    test_server(ws);
    fs::remove_all(ws);
    return failures;
}
