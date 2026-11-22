// dev-atlas test suite: plain stdlib harness, no framework.
#include <chrono>
#include <cstdio>
#include <filesystem>
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
#include "fixture.hpp"
#include "json_lint.hpp"

namespace fs = std::filesystem;
using namespace dev_atlas;

static int failures = 0;
static void check(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++failures; }
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

static void test_csv() {
    auto v = csv("a, b ,,c");
    check(v.size() == 3 && v[0] == "a" && v[1] == "b" && v[2] == "c", "csv trims, drops empty");
    check(csv("").empty() && csv("solo").size() == 1, "csv edge cases");
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
    check(projects.size() == 9, "discover 9 projects");
}

static void test_scan(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    check(result.repos.size() == 9, "scan 9 repos");
    for (const auto& r : result.repos) {
        if (r.display.find("proj-py") != std::string::npos) {
            check(r.stack == "Python", "proj-py stack Python");
            check(!r.git.branch.empty(), "proj-py branch nonempty");
            check(r.git.last_commit_days >= 0, "proj-py last_commit >= 0");
            check(r.git.dirty == 1, "proj-py dirty 1");
            check(r.git.untracked >= 1, "proj-py untracked >=1");
            check(r.bloat_bytes > 0, "proj-py bloat > 0");
            check(!r.bloat.empty() && r.bloat[0].dormant_days == 0,
                  "proj-py bloat age = newest file (0d), not oldest");
        }
        if (r.display.find("proj-node") != std::string::npos)
            check(r.stack == "Node", "proj-node stack Node");
        if (r.display.find("static1") != std::string::npos)
            check(r.stack == "Static", "static1 stack Static");
        if (r.display.find("proj-fresh") != std::string::npos) {
            check(r.git.last_commit_days >= 44, "fresh: commit is ~45d old");
            check(r.last_activity_days == 0, "fresh: activity uses newest event (0d)");
        }
    }
    Bytes ts = 0, tb = 0;
    for (const auto& r : result.repos) {
        ts += r.source_bytes;
        tb += r.bloat_bytes;
    }
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
        if (r.display.find("proj-cold") != std::string::npos)
            cold_bloat = r.bloat_bytes;
    check(plan.total == cold_bloat, "reclaim total == cold bloat (fresh repo excluded)");
    const auto ws2 = ws.parent_path() / (ws.filename().string() + "_copy");
    fs::copy(ws, ws2, fs::copy_options::recursive);
    // fs::copy does not preserve mtimes: restore the cold ages so the copy
    // really is a 40-day-old repo.
    shc("touch -d '40 days ago' " + (ws2 / "labs" / "proj-cold" / "README.md").string());
    shc("touch -d '40 days ago' " + (ws2 / "labs" / "proj-cold" / "node_modules" / "f.bin").string());
    auto plan2 = plan_reclaim(scan({ws2 / "active", ws2 / "labs", ws2 / "archive"}), 30);
    apply_reclaim(plan2);
    check(!fs::exists(ws2 / "labs" / "proj-cold" / "node_modules"), "apply removed node_modules");
    check(fs::exists(ws2 / "labs" / "proj-cold" / "README.md"), "apply kept source");
    check(fs::exists(ws2 / "labs" / "proj-fresh" / "node_modules"), "apply kept fresh bloat");
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
        if (r.display.find("proj-hard") != std::string::npos) {
            check(r.hardcoded_paths.size() == 1, "hard: only src file flagged");
            check(!r.hardcoded_paths.empty() &&
                      r.hardcoded_paths[0].find("conf.py") != std::string::npos,
                  "hard: conf.py flagged, docs/override exempt");
        }
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
              t.find(std::string(home())) == std::string::npos,
          "table: repo present, no absolute home");
}

static void test_render_html(const fs::path& ws) {
    auto roots = {ws / "active", ws / "labs", ws / "archive"};
    auto result = scan(roots);
    fs::path tpl;
    for (const auto& p : {fs::path("templates/dashboard.html"), fs::path("../templates/dashboard.html")})
        if (fs::exists(p)) {
            tpl = p;
            break;
        }
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
    test_csv();
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
