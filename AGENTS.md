# Agent instructions — ESS PostgreSQL

Applies only to `1951123/ess-postgresql`. The research goals and experiment approvals live in [ess-research](https://github.com/1951123/ess-research); this repository independently owns PostgreSQL source, builds and regression tests. Agents must not change other repositories by default.

## Worktree and paths

- Use one Issue → one branch → one Git worktree → one Codex session → one Draft PR. Keep the main checkout stable, and do not merge or destructively clean another checkout without researcher approval.
- Optional worktree directory: `ESS_PG_WORKTREE_PARENT="$HOME/projects/ess-postgresql/.worktrees"`. If unset, require an explicit chosen location; do not infer one. It is not interchangeable with `ESS_WORKTREE_PARENT` (the established ess-research variable).
- Shared original inputs may be read from `ESS_DATA_ROOT`; isolated run files belong beneath `ESS_RUNTIME_PARENT`, separated by repo / Issue / run. Source needed exports from `~/.bashrc` and verify them **in each execution shell**; only require variables a command actually needs. Do not modify a user's shell profile.
- Keep PG build/install and temporary clusters scoped to this worktree or an explicit isolated runtime directory. Never overwrite pinned binaries or stopped clusters belonging to ess-research experiments. Avoid Unix socket paths above the platform's `sun_path` limit; check encoded length before starting a cluster.

## Change, validation and evidence

- Prefer the smallest focused PostgreSQL patch. Do not change estimator, selectivity, clamp, statistics matching or planner behavior inadvertently; diagnostics must be opt-in and independently validated.
- Run focused build/regression tests with their required fixture prerequisites; do not assume a `make check` invocation limits itself to requested named tests. Record the exact commands, build/PG version, observed results and remaining limitations.
- Test and development permission is **not** permission to run ess-research full calibration or official held-out evaluation. Never read held-out outcomes for tuning; ask for explicit approval before creating clusters or doing costly experiments beyond an Issue's bounds.
- For cross-repo integration, document immutable source commit SHA and output contract. The consuming repository independently pins this revision; don't make implicit dependencies on branch HEAD.
- Do not commit database directories, binaries, raw input archives, fixed ANALYZE sample files, large logs, virtual environments or credentials. Retain only necessary bounded test evidence and sanitized diagnostics.
- Keep Draft PRs unmerged until researcher review, with failures and skipped tests accurately reported.
