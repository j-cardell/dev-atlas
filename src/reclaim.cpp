// Reclaim planning and application: dormant bloat only, never source.
#include "dev_atlas/scanner.hpp"

#include <filesystem>
#include <string>

namespace dev_atlas {

ReclaimPlan plan_reclaim(const ScanResult& result, long long older_than_days) {
    ReclaimPlan plan;
    for (const auto& repo : result.repos) {
        if (repo.last_activity_days < older_than_days)
            continue;
        for (const auto& b : repo.bloat)
            if (b.bytes > 0)
                plan.entries.push_back(b);
    }
    for (const auto& e : plan.entries)
        plan.total += e.bytes;
    return plan;
}

void apply_reclaim(const ReclaimPlan& plan) {
    for (const auto& e : plan.entries) {
        std::error_code ec;
        std::filesystem::remove_all(e.path, ec);  // best effort
    }
}

}  // namespace dev_atlas
