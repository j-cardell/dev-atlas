// Output rendering: JSON, table, doctor report, reclaim plan, HTML dashboard.
#pragma once

#include <filesystem>
#include <string>

#include "dev_atlas/common.hpp"
#include "dev_atlas/scanner.hpp"

namespace dev_atlas {

// Full scan as JSON (all paths already ~-collapsed by the data layer).
std::string render_json(const ScanResult& result);
// Aligned plain-text table: name, bucket, stack, health, git, sizes, last activity.
std::string render_table(const ScanResult& result);
// Detailed single-repo audit report (checks, sizes, top files, findings).
std::string render_doctor(const Repo& repo);
// Reclaim plan as text; dry_run=true states nothing was deleted.
std::string render_reclaim(const ReclaimPlan& plan, bool dry_run);
// Substitute the __DATA__ token in the template with render_json(result).
std::string render_html(const ScanResult& result, const std::filesystem::path& template_path);
// Locate templates/dashboard.html: $DEV_ATLAS_TEMPLATES dir, <exe dir>/templates,
// ./templates, then /opt/dev-atlas/templates. Empty path if none exist.
std::filesystem::path find_template();
// Write <out_dir>/latest.html and <out_dir>/scan.json, creating out_dir.
void write_report(const ScanResult& result, const std::filesystem::path& out_dir);
}  // namespace dev_atlas
