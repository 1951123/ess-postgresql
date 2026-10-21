# Agent instructions — ESS PostgreSQL

Applies only to `1951123/ess-postgresql`. The research goals and experiment approvals live in [ess-research](https://github.com/1951123/ess-research); this repository independently owns PostgreSQL source, builds and regression tests. Agents must not change other repositories by default.

## Worktree and paths

- Use one Issue → one branch → one Git worktree → one Codex session → one Draft PR. Keep the main checkout stable, and do not merge or destructively clean another checkout without researcher approval.
- Optional worktree directory: `ESS_PG_WORKTREE_PARENT="$HOME/projects/ess-postgresql/.worktrees"`. If unset, require an explicit chosen location; do not infer one. It is not interchangeable with `ESS_WORKTREE_PARENT` (the established ess-research variable).
- Shared original inputs may be read from `ESS_DATA_ROOT`; isolated run files belong beneath `ESS_RUNTIME_PARENT`, separated by repo / Issue / run. Source needed exports from `~/.bashrc` and verify them **in each execution shell**; only require variables a command actually needs. Do not modify a user's shell profile.
- Keep PG build/install, regression scratch and temporary clusters in the owning experiment's explicit isolated runtime directory, not inside the source Worktree; prefer PostgreSQL out-of-tree builds. Never overwrite pinned binaries or stopped clusters belonging to ess-research experiments. Avoid Unix socket paths above the platform's `sun_path` limit; check encoded length before starting a cluster.

## Change, validation and evidence

- Prefer the smallest focused PostgreSQL patch. Do not change estimator, selectivity, clamp, statistics matching or planner behavior inadvertently; diagnostics must be opt-in and independently validated.
- Run focused build/regression tests with their required fixture prerequisites; do not assume a `make check` invocation limits itself to requested named tests. Record the exact commands, build/PG version, observed results and remaining limitations.
- Test and development permission is **not** permission to run ess-research full calibration or official held-out evaluation. Never read held-out outcomes for tuning; ask for explicit approval before creating clusters or doing costly experiments beyond an Issue's bounds.
- For cross-repo integration, document immutable source commit SHA and output contract. The consuming repository independently pins this revision; don't make implicit dependencies on branch HEAD.
- Do not commit database directories, binaries, raw input archives, fixed ANALYZE sample files, large logs, virtual environments or credentials. Retain only necessary bounded test evidence and sanitized diagnostics.
- Keep Draft PRs unmerged until researcher review, with failures and skipped tests accurately reported.


## Mandatory isolated-build and artifact policy

**ESS_DATA_ROOT is the only shared data root**, read-only for original inputs. Git source commits may be referenced across repositories, but do not consume another Issue's compiled PostgreSQL, install prefix, PGDATA, socket, fixed ANALYZE sample, virtualenv, logs or generated outputs. Each experimental run owns a new pinned source checkout, separate out-of-tree build/install, disposable cluster, and output path under `ESS_RUNTIME_PARENT/ess-postgresql/issue<N>/runs/<run-id>/` (or its consuming experiment's corresponding repository/Issue/run path). Rebuild per experiment, even for identical source commits.

Keep the main checkout on `main`; write source changes only in Issue Worktrees. Configure PostgreSQL builds so all generated build/install/test artifacts stay in the owning runtime/build area whenever supported. If a tool produces generated/ignored files in a source Worktree, Codex must inspect and relocate or safely remove its own generated artifacts, without touching user-owned or other-branch files. `git status --short` alone does not detect ignored debris. Use short Unix socket paths and check actual encoded path length.

Runtime is disposable and need not be maintained by the researcher. Versioned Git evidence should contain minimal source/build pins, tests, necessary scientific observations and reports; never depend on persistent local PG files for reproducibility. Keep large outputs out of Git; request a durable-storage decision before an expensive run if the necessary evidence cannot reasonably be versioned. Older experiments may be described historically, but active experiments must migrate away from cross-Issue shared PG builds before their next substantive run. These conventions do not grant permission to execute experiments or merge.
