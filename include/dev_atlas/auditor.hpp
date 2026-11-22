// Engineering health scoring, live-process mapping, and lifecycle advice.
#pragma once

#include "dev_atlas/common.hpp"

namespace dev_atlas {

// Per-repo health audit. Checks (weights sum to 100):
//   README.md present (15), AGENTS.md present (10), .gitignore present (10),
//   no source/header file in src|include|scripts|lib over 350 lines (20;
//   top 3 largest listed in repo.top_files), no hardcoded /home/<user>/
//   machine paths (15; up to 5 samples in repo.hardcoded_paths), gitleaks
//   clean when the tool is installed (15; repo.gitleaks states clean /
//   "N finding(s)" / unavailable / error), clean working tree (10),
//   up to date with remote (5). Sets repo.health_score (0-100) and
//   repo.lifecycle: labs repos untouched >30 days -> "archive", labs repos
//   passing every check -> "promote", otherwise "".
void audit_repo(Repo& repo);

// audit_repo for every repo, then map running rootless podman containers
// (podman ps) and their published local dev ports back to the repo whose
// basename matches the container name. No-op when podman is absent.
void audit_all(ScanResult& result);
}  // namespace dev_atlas
