# AGENTS.md — dev-atlas

## Architecture & Anti-Monolith Rules

- Keep source files **<= 350 lines** and functions **<= 50 lines** (split by responsibility before a file grows past 350 lines).
- Keep the entrypoint thin (config + wiring only); separate domain logic, I/O, and UI/templates into distinct modules.
- Use relative paths (`__file__` / `BASH_SOURCE[0]`) for intra-repo files; use env vars + `.env.example` only for external paths or secrets.
- Put rendered outputs, screenshots, and temp files in `out/` (gitignored).
- When working on this repo in parallel (multiple editors or CLIs, or another session has uncommitted edits), isolate work in a worktree (`git worktree add /tmp/wt-dev-atlas-<task> -b <branch>`) and run `git worktree remove` after merging.
- Before considering work done (and again after merging), run a 3-step cleanup pass: (1) verify correctness with real test/CLI runs, (2) delete dead code, unused scaffold placeholders, and duplicate helpers across modules, and (3) remove comments that restate the code, silent exception handlers, and filler prose.

## Verification

- Secret scan: `command -v gitleaks >/dev/null && gitleaks dir . --no-banner --redact || true`
- Hardcoded-path gate: `! grep -rnE '(/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|/mnt/[A-Za-z0-9._-]+/|/media/[A-Za-z0-9._-]+/|/run/media/[A-Za-z0-9._-]+/)' . --exclude-dir=.git --exclude-dir=tests --exclude-dir=docs --exclude-dir=out --exclude-dir=.venv --exclude-dir=venv --exclude-dir=node_modules --exclude-dir=build --exclude-dir=__pycache__ --exclude-dir=.ruff_cache --exclude-dir=.pytest_cache --exclude='*.md' --exclude='.env.example' --exclude='compose.override*' 2>/dev/null`
- File line gate (any language): `find src include tests scripts lib -type f 2>/dev/null | while IFS= read -r f; do n=$(wc -l < "$f"); [ "$n" -gt 350 ] && echo "$f: $n lines" && exit 1; done; true`
- Stack lint/test: `cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure && clang-tidy -p build src/*.cpp`
