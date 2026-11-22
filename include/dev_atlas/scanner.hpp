// Workspace discovery, git status, disk split, and reclaim planning.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "dev_atlas/common.hpp"

namespace dev_atlas {

// Rebuildable bloat directory names. Exact matches: .venv, node_modules,
// build, target, out, .ruff_cache, .pytest_cache. Prefix match: venv*
// (venv, venv2, ...).
const std::vector<std::string>& bloat_names();
bool is_bloat_name(const std::string& name);

// Discover project roots under the given bucket directories (e.g.
// $HOME/development/active). A project is any directory containing a
// .git (file or directory), plus direct bucket children that are
// directories with no .git and no git repo descendants (plain static
// projects). Group dirs (no .git, git descendants deeper down) are not
// projects; their nested repos each are. Hidden dirs, bloat dirs, and
// .worktrees are skipped during the walk.
std::vector<std::filesystem::path> discover_projects(const std::vector<std::filesystem::path>& roots);

// Fill stack, git info, source/bloat split, bloat entries, and
// last_activity_days for one project root. Thread-safe: no shared state,
// repo.path/bucket/display must be set by the caller first.
void scan_repo(Repo& repo);

// Parallel scan (std::async worker per repo) of every discovered project.
// Sets scanned_at and the size totals.
ScanResult scan(const std::vector<std::filesystem::path>& roots);

struct ReclaimPlan {
    std::vector<BloatEntry> entries;  // bloat dirs in repos dormant >= older_than_days
    Bytes total = 0;
};

// Plan dormant bloat for repos untouched for at least older_than_days.
// Pure: never deletes. Entries point only at bloat directories, so tracked
// and uncommitted source files can never be planned.
ReclaimPlan plan_reclaim(const ScanResult& result, long long older_than_days);

// Delete every planned bloat directory (std::filesystem::remove_all).
void apply_reclaim(const ReclaimPlan& plan);
}  // namespace dev_atlas
