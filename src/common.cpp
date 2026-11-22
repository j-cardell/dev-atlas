// Shared utilities: display paths, JSON, byte formatting, command runner,
// and file statistics.
#include "dev_atlas/common.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dev_atlas {

const char* home() {
    const char* e = std::getenv("HOME");
    return e ? e : "";
}

std::string collapse_home(const std::string& path) {
    const std::string h = home();
    if (h.empty())
        return path;
    if (path == h)
        return "~";
    if (path.size() > h.size() + 1 && std::equal(h.begin(), h.end(), path.begin()) &&
        path[h.size()] == '/')
        return "~" + path.substr(h.size());
    return path;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string human_bytes(Bytes bytes) {
    if (bytes < 1024) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%lld B", static_cast<long long>(bytes));
        return buf;
    }
    const char* units[] = {"KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes) / 1024.0;
    int i = 0;
    while (v >= 1024.0 && i < 3) {
        v /= 1024.0;
        ++i;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[i]);
    return buf;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

static bool exec_with_pipe(const std::vector<std::string>& args, std::string& out, int* rc,
                           const std::filesystem::path* cwd) {
    out.clear();
    if (args.empty())
        return false;
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0)
        return false;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], 1);
        close(pipe_fd[1]);
        if (cwd) {
            std::error_code ec;
            std::filesystem::current_path(*cwd, ec);
            if (ec)
                _exit(126);
        }
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& a : args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(pipe_fd[1]);
    char buf[8192];
    ssize_t n;
    while ((n = read(pipe_fd[0], buf, sizeof buf)) > 0)
        out.append(buf, static_cast<size_t>(n));
    close(pipe_fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (rc)
        *rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
}

bool run_argv(const std::vector<std::string>& args, std::string& out, int* rc) {
    return exec_with_pipe(args, out, rc, nullptr);
}

bool run_argv_in(const std::vector<std::string>& args, const std::filesystem::path& cwd,
                 std::string& out, int* rc) {
    return exec_with_pipe(args, out, rc, &cwd);
}

bool run_cmd(const std::string& line, std::string& out, int* rc) {
    std::vector<std::string> args;
    std::string tok;
    for (const char c : line) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!tok.empty()) {
                args.push_back(tok);
                tok.clear();
            }
        } else {
            tok += c;
        }
    }
    if (!tok.empty())
        args.push_back(tok);
    return run_argv(args, out, rc);
}

Bytes dir_size_bytes(const std::filesystem::path& p) {
    Bytes total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             p, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec))
            total += static_cast<Bytes>(entry.file_size(ec));
    }
    return total;
}

long long days_since(const std::filesystem::file_time_type& t) {
    const auto now = std::filesystem::file_time_type::clock::now();
    if (t >= now)
        return 0;
    return std::chrono::duration_cast<std::chrono::days>(now - t).count();
}

std::optional<std::filesystem::file_time_type> file_mtime(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec)
        return std::nullopt;
    return t;
}

long long count_lines(const std::filesystem::path& p) {
    std::ifstream f(p);
    long long n = 0;
    std::string line;
    while (std::getline(f, line))
        ++n;
    return n;
}

}  // namespace dev_atlas
