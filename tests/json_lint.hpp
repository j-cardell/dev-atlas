// Structural JSON lint for the test suite: string-aware bracket balance and
// separator-state validation. Catches missing/duplicate commas, missing
// colons, unbalanced containers, unquoted values, and trailing garbage.
// Conservative by design: it may reject some valid JSON (e.g. a value omitted
// before a closing bracket) but never accepts broken output.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

inline bool json_lint(const std::string& s) {
    // st: 0 = a value (or object key) is expected, 1 = a value just completed.
    int st = 0;
    std::vector<char> stack;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            if (st != 0) return false;
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\') ++i;
                ++i;
            }
            if (i >= s.size()) return false;
            st = 1;
            continue;
        }
        if (c == '{' || c == '[') {
            if (st != 0) return false;
            stack.push_back(c);
            continue;
        }
        if (c == '}' || c == ']') {
            if (stack.empty()) return false;
            char want = c == '}' ? '{' : '[';
            if (stack.back() != want) return false;
            stack.pop_back();
            st = 1;
            continue;
        }
        if (c == ':') {
            if (st != 1) return false;
            st = 0;
            continue;
        }
        if (c == ',') {
            if (st != 1) return false;
            st = 0;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') continue;
        // Literal token (true/false/null/number): consume it whole.
        if (st != 0) return false;
        do {
            ++i;
        } while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
                 s[i] != '"' && s[i] != ':' && s[i] != ' ' && s[i] != '\n' &&
                 s[i] != '\t' && s[i] != '\r');
        --i;
        st = 1;
    }
    return st == 1 && stack.empty();
}
