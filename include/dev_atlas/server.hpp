// Embedded POSIX HTTP server for the dashboard.
#pragma once

#include <filesystem>

namespace dev_atlas {

// Blocking accept loop on 127.0.0.1:<port>, one thread per connection.
//   GET /              -> html_path  (text/html)
//   GET /api/scan.json -> json_path  (application/json)
// everything else 404. Returns 0 on clean shutdown (SIGINT/SIGTERM) and
// -1 when bind fails.
int serve(const std::filesystem::path& html_path, const std::filesystem::path& json_path, int port);
}  // namespace dev_atlas
