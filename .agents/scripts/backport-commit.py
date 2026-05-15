#!/usr/bin/env python3
"""Backport a merged commit to one or more release branches.

Python rewrite of backport-commit.sh. Same CLI surface (flags, positional
arguments) and same exit-code contract:

    0  all branches succeeded; PR URLs printed on stdout
    1  pre-flight failure (bad args, dirty workspace, missing remote, ...)
    2  cherry-pick conflict OR lint failure on a task branch -- the user
       resolves / fixes lint, amends, and re-runs with `-x <branch>`

Invoked via the bash shim `.agents/scripts/backport-commit.sh`.

Env overrides:
    YB_BACKPORT     workspace dir; default $HOME/code/backport-ybdb
                    (created as a git worktree off this script's repo,
                    sharing .git so fetches don't duplicate the object DB)
    YB_GH_REPO      upstream owner/name; default yugabyte/yugabyte-db
    YB_GH_FORK      <owner>/<repo> of the fork; default <gh-user>/<repo>
    YB_FORK_URL     full URL of the fork; defaults to mirror upstream protocol
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn, Optional


# --- Process / git plumbing -------------------------------------------------

class CmdError(Exception):
    """A subprocess exited non-zero. .returncode and .output are populated."""
    def __init__(self, cmd, returncode, output, stderr):
        super().__init__(f"command failed (rc={returncode}): {shlex.join(cmd)}")
        self.cmd = cmd
        self.returncode = returncode
        self.output = output
        self.stderr = stderr


def run(cmd, *, cwd=None, check=True, env=None, input_=None):
    """Run a command. By default raises CmdError on non-zero exit.

    Returns CompletedProcess with .stdout / .stderr captured (text).
    """
    proc = subprocess.run(
        cmd, cwd=cwd, env=env, input=input_,
        capture_output=True, text=True,
    )
    if check and proc.returncode != 0:
        raise CmdError(cmd, proc.returncode, proc.stdout, proc.stderr)
    return proc


def git(*args, cwd=None, check=True):
    """Run `git <args>` and return stdout (stripped)."""
    return run(["git", *args], cwd=cwd, check=check).stdout.strip()


def git_ok(*args, cwd=None) -> bool:
    """True iff `git <args>` exits 0."""
    return run(["git", *args], cwd=cwd, check=False).returncode == 0


def gh_json(args, *, check=True):
    """Run `gh api <args>` and parse stdout as JSON. Returns None on failure."""
    proc = run(["gh", "api", *args], check=False)
    if proc.returncode != 0:
        if check:
            raise CmdError(["gh", "api", *args], proc.returncode, proc.stdout, proc.stderr)
        return None
    if not proc.stdout.strip():
        return None
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError:
        # `gh api` occasionally exits 0 with a plain-text body (e.g. for
        # endpoints that returned a 204 with a status line, or pre-flight
        # error pages from a proxy). Treat that the same as no data.
        return None


def info(msg: str) -> None:
    """Print a `==== ...` info line (matches the bash script's output style)."""
    print(f"==== {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"WARN: {msg}", file=sys.stderr, flush=True)


def fail(msg: str, code: int = 1) -> NoReturn:
    print(f"ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


# --- Configuration ----------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
SCRIPT_REPO_ROOT = SCRIPT_DIR.parent.parent  # /<worktree>


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="backport-commit",
        description="Backport a merged commit to one or more release branches.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("-n", "--preview", action="store_true",
                   help="Prepare task branches but don't push or create PRs.")
    p.add_argument("-l", "--local", action="store_true",
                   help="Use the current workspace instead of creating a worktree.")
    p.add_argument("-r", "--reviewers", default=None,
                   help="Comma/space separated reviewer list. -r '' disables.")
    p.add_argument("-s", "--stable-limit", default=None,
                   help="Backport to all stable branches >= this branch.")
    p.add_argument("-x", "--accept-branch", action="append", default=[],
                   help="Skip cherry-pick on the given branch (already resolved).")
    p.add_argument("commit", help="Commit SHA (>=7 hex chars).")
    p.add_argument("branches", nargs="*", help="Destination branches.")
    args = p.parse_args(argv)

    if not re.fullmatch(r"[0-9a-fA-F]{7,64}", args.commit):
        p.error(f"malformed commit ID: {args.commit!r} (expected 7+ hex chars)")
    if args.stable_limit and args.branches:
        p.error("cannot mix -s / --stable-limit with positional branches")
    return args


def normalize_reviewers(s: Optional[str]) -> Optional[str]:
    """Accept space- or comma-separated, return comma-separated trimmed.

    Returns None if input is None (no -r given). Returns "" if input was -r ''
    (explicit disable).
    """
    if s is None:
        return None
    parts = [p for p in re.split(r"[ ,]+", s) if p]
    return ",".join(parts)


# --- Workspace + remote detection -------------------------------------------

def setup_workspace(workspace: Path, use_local: bool) -> None:
    """Set up workspace and cd into it.

    For a fresh path, creates a git worktree off SCRIPT_REPO_ROOT (the
    repo containing this script). The worktree shares .git with the
    source repo, so the object database isn't duplicated and fetched
    refs are visible across worktrees. An existing path is reused
    as-is, whether it's a worktree, a clone, or a plain checkout --
    we trust it as long as it's a valid git working directory.
    """
    if use_local:
        info(f"Using current workspace: {workspace}")
    elif workspace.exists():
        if not git_ok("rev-parse", "--is-inside-work-tree", cwd=workspace):
            fail(f"workspace path exists but is not a git working tree: "
                 f"{workspace}. Remove it (or set YB_BACKPORT to a fresh "
                 f"path) and re-run.")
        info(f"Using workspace: {workspace}")
    else:
        info(f"Creating worktree: {workspace} (from {SCRIPT_REPO_ROOT})")
        workspace.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "-C", str(SCRIPT_REPO_ROOT), "worktree", "add",
             "--detach", str(workspace)])

    os.chdir(workspace)


def detect_upstream_remote(gh_repo: str) -> str:
    """Find the git remote whose URL points at $gh_repo."""
    override = os.environ.get("UPSTREAM_REMOTE")
    if override:
        return override
    remotes = git("remote").splitlines()
    for r in remotes:
        if not r:
            continue
        url = git("remote", "get-url", r, check=False)
        if gh_repo in url:
            return r
    fail(f"no git remote points at {gh_repo}; "
         f"add one with: git remote add upstream git@github.com:{gh_repo}.git")


def check_clean_tree() -> None:
    """Refuse to run if there are tracked-file changes (untracked is fine)."""
    porcelain = git("status", "--porcelain")
    dirty = [line for line in porcelain.splitlines() if not line.startswith("??")]
    if dirty:
        fail("workspace has uncommitted tracked changes; "
             "stash or commit first:\n" + "\n".join(dirty), code=1)


def ensure_commit_local(commit: str, upstream_remote: str) -> str:
    """Make sure we have the commit; fetch by SHA if necessary. Returns full SHA."""
    if not git_ok("cat-file", "-e", commit):
        info("Fetching commit")
        proc = run(["git", "fetch", upstream_remote, commit], check=False)
        if proc.returncode != 0:
            warn(f"'git fetch {upstream_remote} {commit}' failed "
                 "(server may not allow SHA fetches):")
            for line in (proc.stderr or proc.stdout).splitlines():
                warn(f"      {line}")
    if not git_ok("cat-file", "-e", commit):
        fail(f"commit {commit} is not present locally and could not be fetched. "
             f"Make sure it is merged to a branch reachable from {upstream_remote}, "
             f"then run 'git fetch {upstream_remote}' and retry.")
    return git("rev-parse", commit)


def lookup_originating_pr(gh_repo: str, commit: str) -> tuple[Optional[int], Optional[str]]:
    """Return (pr_number, pr_url) for the merge PR of $commit, or (None, None)."""
    info(f"Looking up originating PR for {commit[:9]}")
    data = gh_json([f"repos/{gh_repo}/commits/{commit}/pulls"], check=False)
    if not data:
        print("No originating PR found; backport body will reference the commit hash only.")
        return None, None
    pr = data[0]
    print(f"Found originating PR: #{pr['number']} -> {pr['html_url']}")
    return pr["number"], pr["html_url"]


def detect_auto_reviewers(gh_repo: str, prnum: int, self_login: str) -> str:
    """Union of pending review requests and actual reviewers, minus self / bots."""
    upstream_owner = gh_repo.split("/", 1)[0]
    pending_data = gh_json([f"repos/{gh_repo}/pulls/{prnum}/requested_reviewers"], check=False) or {}
    # GitHub returns null entries for deleted users; (x or {}).get(...) is
    # a cheap guard so a deleted requester / reviewer doesn't crash the run.
    user_logins = [u["login"] for u in (pending_data.get("users") or [])
                   if (u or {}).get("type") == "User"]
    team_slugs = [f"{upstream_owner}/{t['slug']}"
                  for t in (pending_data.get("teams") or []) if t]

    review_data = gh_json([f"repos/{gh_repo}/pulls/{prnum}/reviews"], check=False) or []
    # `r and` defends against the rare null entry in the review array
    # itself; `(r.get("user") or {})` defends against r["user"] being null
    # for deleted reviewers. Both are observed in the wild for users that
    # have been GDPR-erased.
    reviewer_logins = [r["user"]["login"] for r in review_data
                       if r and (r.get("user") or {}).get("type") == "User"]

    # Dedup, drop self.
    seen = set()
    out: list[str] = []
    for x in user_logins + team_slugs + reviewer_logins:
        if x and x != self_login and x not in seen:
            seen.add(x)
            out.append(x)
    return ",".join(out)


def resolve_fork(gh_repo: str, gh_user: str, upstream_remote: str) -> tuple[str, str, str]:
    """Return (gh_fork, fork_owner, push_url). Aborts if fork == upstream owner."""
    upstream_owner = gh_repo.split("/", 1)[0]
    repo_name = gh_repo.split("/", 1)[1]

    gh_fork = os.environ.get("YB_GH_FORK") or ""
    if not gh_fork:
        if not gh_user or gh_user == upstream_owner:
            fail("no personal fork detected for the authenticated gh user. "
                 "Set YB_GH_FORK=<your-user>/<repo> or run 'gh repo fork' and re-run.")
        gh_fork = f"{gh_user}/{repo_name}"

    fork_owner = gh_fork.split("/", 1)[0]
    if fork_owner == upstream_owner:
        fail(f"YB_GH_FORK '{gh_fork}' resolves to the upstream owner ({upstream_owner}). "
             f"Refusing to push to the upstream repo.")

    info(f"Pushing to fork: {gh_fork} (PRs target {gh_repo})")

    # Build push URL: env override wins; otherwise mirror upstream's protocol.
    push_url = os.environ.get("YB_FORK_URL")
    if not push_url:
        upstream_url = git("remote", "get-url", upstream_remote, check=False)
        if upstream_url.startswith("https://"):
            push_url = f"https://github.com/{gh_fork}.git"
        else:
            push_url = f"git@github.com:{gh_fork}"
    info(f"Fork URL: {push_url}")
    return gh_fork, fork_owner, push_url


# --- Per-branch work --------------------------------------------------------

class ResumableConflict(Exception):
    """Raised when cherry-pick hits a non-empty conflict; user must resolve."""


def task_branch_name(commit: str, branch: str) -> str:
    return f"backport-{commit[:9]}-{branch}"


def existing_pr_url(gh_repo: str, head: str) -> Optional[str]:
    """If a PR with this head already exists, return its URL; otherwise None."""
    data = gh_json([
        f"repos/{gh_repo}/pulls",
        "-X", "GET",
        "-F", f"head={head}",
        "-F", "state=all",
        "-F", "per_page=1",
    ], check=False)
    if data and isinstance(data, list) and data:
        return data[0].get("html_url")
    return None


def checkout_task_branch(task: str, upstream_remote: str, branch: str) -> None:
    """Create or check out the task branch."""
    info(f"Checking out task branch: {task}")
    if git_ok("show-ref", "--verify", "--quiet", f"refs/heads/{task}"):
        run(["git", "checkout", task])
    else:
        run(["git", "checkout", "-b", task, f"{upstream_remote}/{branch}"])


def apply_cherry_pick(port_cmd: list[str], task: str, workspace: Path,
                      branch: str, rerun_hint: str) -> bool:
    """Run cherry-pick. Returns True if a real commit was made; False if no-op.

    Raises ResumableConflict on real conflicts (caller exits 2 with hint).
    """
    info(f"Applying code change: {shlex.join(port_cmd)}")
    proc = run(port_cmd, env={**os.environ, "LC_ALL": "C"}, check=False)
    if proc.returncode == 0:
        return True
    combined = (proc.stdout or "") + (proc.stderr or "")
    if "nothing to commit" in combined or "previous cherry-pick is now empty" in combined:
        info(f"Cherry-pick was a no-op on {branch} (already present); skipping.")
        run(["git", "cherry-pick", "--skip"], check=False)
        return False
    print(combined.rstrip(), file=sys.stderr)
    print()
    info(f"Backport ERROR: Merge conflicts?")
    info(f"Workspace: {workspace}")
    info(f"Task branch: {task}")
    info("Resolve conflicts, commit (keep original message), and re-run.")
    info(rerun_hint)
    raise ResumableConflict()


def amend_message(commit: str, branch: str, pr_url: Optional[str]) -> None:
    """Rewrite the just-cherry-picked commit's subject/body."""
    info("Amending commit message")
    raw_subject = git("log", "-n1", "--format=%s", commit)
    # Strip any pre-existing [BACKPORT ...] tag.
    subject = re.sub(r"\[backport[^\]]*\]\s*", "", raw_subject, flags=re.IGNORECASE)
    # Most commit subjects begin with their own bracketed tag like
    # `[#31404] YSQL: ...` -- keep the BACKPORT tag flush against it
    # (no space between `]` and `[`). Fall back to a single space for
    # subjects that don't lead with a bracket.
    sep = "" if subject.startswith("[") else " "
    new_subject = f"[BACKPORT {branch}]{sep}{subject}"

    # Body: original commit's body, minus any prior Backport-of / Original-commit
    # footers, with `Original commit: <SHA> / #<PR>` inserted above the `## Test plan`
    # heading (if present) or appended at the end.
    raw_body = git("log", "-n1", "--format=%b", commit)
    body_lines = [
        line for line in raw_body.splitlines()
        if not (line.startswith("Backport of:")
                or line.startswith("Original PR:")
                or line.startswith("Original commit:"))
    ]
    pr_num_match = re.search(r"/pull/(\d+)", pr_url) if pr_url else None
    if pr_num_match:
        original_line = f"Original commit: {commit} / #{pr_num_match.group(1)}"
    else:
        original_line = f"Original commit: {commit}"
    if any(line.startswith("## Test plan") for line in body_lines):
        new_body_lines: list[str] = []
        inserted = False
        for line in body_lines:
            if not inserted and line.startswith("## Test plan"):
                new_body_lines.append(original_line)
                new_body_lines.append("")
                inserted = True
            new_body_lines.append(line)
        new_body = "\n".join(new_body_lines)
    else:
        # If body_lines is empty (rare: original commit had no body beyond
        # the prior footers we just stripped), avoid a leading blank line.
        prefix = "\n".join(body_lines).rstrip()
        new_body = (prefix + "\n\n" if prefix else "") + original_line

    run(["git", "commit", "--amend", "--cleanup=verbatim",
         "-m", new_subject, "-m", new_body])


def run_lint(workspace: Path, upstream_remote: str, branch: str, task: str) -> None:
    """Run the workspace's own lint.sh, with whatever flag it supports.

    Master's lint.sh is a thin wrapper around lint.py that takes `--rev <base>`.
    Release branches (2026.1, 2025.x, 2024.x) ship a different lint.sh -- a
    cpplint runner that takes `-c` / `--changed-only`. We try `--rev` first
    and fall back to `-c` if the flag isn't recognized, so the script works
    on both regimes without copying files around.

    Raises ResumableConflict on lint failure.
    """
    bp_lint = workspace / "build-support" / "lint.sh"
    if not bp_lint.is_file() or not os.access(bp_lint, os.X_OK):
        warn(f"{bp_lint} not found or not executable; skipping lint check.")
        return

    info(f"Running linter on {task} (vs {upstream_remote}/{branch})")
    # LC_ALL=C keeps the "unknown flag: --rev" probe locale-independent
    # so the fallback path triggers on any system, not just English ones.
    lint_env = {**os.environ, "LC_ALL": "C"}
    proc = run([str(bp_lint), "--rev", f"{upstream_remote}/{branch}"],
               env=lint_env, check=False)
    combined = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0 and "unknown flag: --rev" in combined:
        info("lint.sh on this branch doesn't recognize --rev; "
             "falling back to '-c' (changed-only) for the older cpplint wrapper.")
        proc = run([str(bp_lint), "-c"], env=lint_env, check=False)
        combined = (proc.stdout or "") + (proc.stderr or "")
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="", file=sys.stderr)
    rc = proc.returncode

    if rc != 0:
        print()
        info(f"Backport ERROR: Lint failed on {task}.")
        info(f"Workspace: {workspace}")
        info(f"Task branch: {task}")
        info("Fix the lint issues and AMEND them into the backport commit:")
        info("  git add <files>")
        info("  git commit --amend --no-edit")
        info(f"Then re-run with -x {branch} to publish.")
        info("")
        info("(The task branch is unpublished, so amending is safe -- and")
        info("required: each task branch must be a single commit so the")
        info("chain logic at the bottom of the loop, which cherry-picks the")
        info("TIP of $task_branch onto the next branch, picks up the full")
        info("change. The src/AGENTS.md \"never amend\" rule applies to public")
        info("commits, not to in-flight task branches.)")
        raise ResumableConflict()


def push_and_create_pr(*, gh_repo: str, push_url: str, pr_head: str, base: str,
                        task: str, title: str, body: str) -> str:
    """Push the task branch and open the PR. Returns PR URL."""
    info(f"Publishing backport PR for branch {task}")
    run(["git", "push", "-u", push_url, task])
    proc = run(
        ["gh", "pr", "create",
         "-R", gh_repo, "-B", base, "-H", pr_head,
         "-t", title, "-F", "-"],
        input_=body,
    )
    pr_url = proc.stdout.strip()
    print(pr_url)
    return pr_url


def request_reviewers(gh_repo: str, pr_num: int, reviewers: str) -> None:
    """Split reviewers into users[] vs team_reviewers[] and POST."""
    if not reviewers:
        return
    info(f"Requesting reviewers: {reviewers}")
    user_logins: list[str] = []
    team_slugs: list[str] = []
    for r in reviewers.split(","):
        r = r.strip()
        if not r:
            continue
        if "/" in r:
            team_slugs.append(r.split("/", 1)[1])
        else:
            user_logins.append(r)

    payload: dict[str, list[str]] = {}
    if user_logins:
        payload["reviewers"] = user_logins
    if team_slugs:
        payload["team_reviewers"] = team_slugs
    if not payload:
        return

    proc = run(
        ["gh", "api", "-X", "POST",
         f"repos/{gh_repo}/pulls/{pr_num}/requested_reviewers",
         "--input", "-"],
        input_=json.dumps(payload),
        check=False,
    )
    if proc.returncode != 0:
        warn(f"failed to add reviewers to PR #{pr_num} (continuing): {reviewers}")


# --- Main -------------------------------------------------------------------

def detect_stable_branches(upstream_remote: str, stable_limit: Optional[str]) -> list[str]:
    """Return the list of stable+preview release branches to back-port to."""
    # Try the release service first; tolerate a missing curl by falling
    # straight through to the local-ref scan (subprocess.run raises
    # FileNotFoundError when the binary isn't on PATH, so guard the call).
    names: list[str] = []
    if shutil.which("curl"):
        proc = run(
            ["curl", "-s", "--fail", "--connect-timeout", "5", "--max-time", "10",
             "https://release.dev.yugabyte.com/version/active/text"],
            check=False,
        )
        if proc.returncode == 0 and proc.stdout.strip():
            names = [n.strip() for n in proc.stdout.replace(",", " ").split() if n.strip()]
    if not names:
        # Fall back to local refs matching the release naming scheme.
        # Match any 2.x or YYYY.N -- preview/stable split happens below
        # via is_preview, so the regex shouldn't pre-filter by parity.
        all_remote = git("branch", "-a")
        pattern = re.compile(rf"^remotes/{re.escape(upstream_remote)}/"
                             r"(2\.[0-9]+|[0-9]{4}\.[0-9]+)$")
        for line in all_remote.splitlines():
            line = line.strip()
            m = pattern.match(line)
            if m:
                names.append(m.group(1))
    # Preview (odd 2.NN) first, then stable, each newest-first.
    # `[0-9]*` lets the trailing-odd-digit class match single-digit
    # minors too (2.1, 2.3); the `$` anchor keeps "2.20foo"-style
    # garbage from being misclassified.
    def is_preview(n: str) -> bool:
        return bool(re.match(r"^2\.[0-9]*[13579]$", n))
    # Sort by numeric components, not lexicographic. Lex sort puts
    # "2024.10" before "2024.2" (since "1" < "2") which would corrupt
    # the order new releases land in once minors hit double digits.
    def version_key(v: str) -> list[int]:
        return [int(p) for p in re.findall(r"\d+", v)]
    preview = sorted({n for n in names if is_preview(n)}, key=version_key, reverse=True)
    stable = sorted({n for n in names if not is_preview(n)}, key=version_key, reverse=True)
    ordered = preview + stable
    if stable_limit:
        out: list[str] = []
        for b in ordered:
            out.append(b)
            if b == stable_limit:
                break
        return out
    return ordered


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    gh_repo = os.environ.get("YB_GH_REPO", "yugabyte/yugabyte-db")
    # `.resolve()` makes a relative `YB_BACKPORT` absolute. Without this,
    # `git -C SCRIPT_REPO_ROOT worktree add <relative>` would create the
    # worktree under SCRIPT_REPO_ROOT while `os.chdir(workspace)` would
    # try to enter `<relative>` from this process's cwd -- two different
    # locations if cwd != SCRIPT_REPO_ROOT.
    workspace = Path(os.environ.get(
        "YB_BACKPORT", str(Path.home() / "code/backport-ybdb"))).resolve()
    if args.local:
        workspace = Path.cwd()

    if not shutil.which("gh"):
        fail("'gh' CLI is required but not found in PATH.")
    if not shutil.which("curl"):
        warn("'curl' not found; release-service auto-discover unavailable -- "
             "will fall back to scanning local remote refs. "
             "Pass branches explicitly to skip auto-discovery entirely.")

    # Workspace + clean check.
    setup_workspace(workspace, args.local)
    upstream_remote = detect_upstream_remote(gh_repo)
    info(f"Fetching latest code from {upstream_remote}")
    run(["git", "fetch", upstream_remote])

    # Build push URL (needs upstream remote).
    gh_user_proc = run(["gh", "api", "user", "--jq", ".login"], check=False)
    gh_user = gh_user_proc.stdout.strip() if gh_user_proc.returncode == 0 else ""
    gh_fork, fork_owner, push_url = resolve_fork(gh_repo, gh_user, upstream_remote)

    check_clean_tree()
    full_commit = ensure_commit_local(args.commit, upstream_remote)
    short = full_commit[:9]
    prnum, pr_url = lookup_originating_pr(gh_repo, full_commit)

    # Auto-detect reviewers from the originating PR.
    auto_reviewers = ""
    if prnum is not None:
        auto_reviewers = detect_auto_reviewers(gh_repo, prnum, gh_user)
        if auto_reviewers:
            print(f"Auto-detected reviewers from PR #{prnum}: {auto_reviewers}")

    # Resolve final reviewer list. -r overrides; -r '' explicitly disables.
    explicit_rev = normalize_reviewers(args.reviewers)
    final_reviewers = explicit_rev if explicit_rev is not None else auto_reviewers

    # Detect merge commit (>1 parent) so we cherry-pick with -m 1.
    parents = git("rev-list", "--parents", "-n1", full_commit).split()
    parent_count = len(parents) - 1
    base_port_cmd = ["git", "cherry-pick", "--allow-empty", full_commit]
    if parent_count > 1:
        base_port_cmd = ["git", "cherry-pick", "-m", "1", "--allow-empty", full_commit]

    # Decide branch list.
    interactive_prompt = False
    branches = list(args.branches)
    if not branches:
        branches = detect_stable_branches(upstream_remote, args.stable_limit)
        if not args.stable_limit:
            interactive_prompt = True
    if not branches:
        fail("no destination branches given and auto-discovery returned none.")

    # Clean up any leftover cherry-pick state from a prior run.
    cherry_head = Path(git("rev-parse", "--git-path", "CHERRY_PICK_HEAD")).resolve()
    if cherry_head.is_file():
        # If there are pending tracked changes, refuse -- might be the user's
        # in-progress conflict resolution.
        cached = git("diff", "--cached", "--name-only")
        unstaged = git("diff", "--name-only")
        if cached or unstaged:
            fail("cherry-pick in progress with uncommitted changes. "
                 "'git cherry-pick --continue' to commit, or '--abort' to discard.",
                 code=1)
        info("No pending changes; skipping empty cherry-pick.")
        run(["git", "cherry-pick", "--skip"], check=False)

    # Per-branch loop. The chain logic: after each successful branch the next
    # one cherry-picks from the previous task branch's tip (so any conflict
    # resolution carries forward).
    port_cmd = base_port_cmd
    rerun_args = []
    if args.preview:    rerun_args.append("-n")
    if args.local:      rerun_args.append("-l")
    if explicit_rev is not None:
        rerun_args.extend(["-r", explicit_rev])
    if args.stable_limit:
        rerun_args.extend(["-s", args.stable_limit])
    rerun_args.append(full_commit)
    # When the user originally invoked without explicit branches and
    # without -s, the auto-discovered list (`branches`) was confirmed
    # interactively. A rerun hint that omits those branches would re-
    # trigger auto-discovery and the script would block waiting for an
    # interactive prompt the rerun environment can't answer. Pin the
    # resolved set explicitly. When -s was used the rerun re-derives
    # the same list from the same -s, so leave args.branches (empty)
    # there.
    rerun_args.extend(args.branches if (args.branches or args.stable_limit)
                      else branches)

    for branch in branches:
        if branch == "master":
            continue
        print()
        print(f"================== Release Branch {branch}")
        if interactive_prompt and sys.stdin.isatty():
            ans = input(f"Backport {short} to branch {branch} [Y/n]? ").strip()
            if ans.lower().startswith("n"):
                continue
        elif interactive_prompt:
            fail("auto-discovery in non-interactive mode; pass branches explicitly.")

        task = task_branch_name(full_commit, branch)
        pr_head = f"{fork_owner}:{task}" if fork_owner else task

        # Skip if a backport PR for this head already exists.
        try:
            existing = existing_pr_url(gh_repo, pr_head)
        except Exception:
            existing = None
        if existing:
            info(f"Existing backport PR found: {existing} -- skipping")
            # The chain logic below cherry-picks from $task onto the next
            # branch; ensure $task is present locally and current with the
            # fork. Force-fetch (`+task:task`) so a stale local task ref
            # left over from a previous run gets bumped to the fork's tip
            # instead of silently chaining from outdated code. If the
            # fetch fails (branch deleted on the fork, or PR was opened
            # from a different fork), fall back to chaining from the
            # original commit so subsequent branches still apply something
            # sensible -- they may need conflict resolution again.
            # Guard against silently rewinding the user's working tree
            # if they happen to be on the task branch right now (e.g.
            # mid-conflict-resolution after a prior script run died).
            # Force-fetching `+task:task` would advance the ref under
            # their feet without touching the working tree, leaving them
            # confused. In that case, trust the local state instead.
            current_head = git("symbolic-ref", "--short", "HEAD", check=False)
            if current_head == task:
                info(f"Currently on {task}; using local state (no fetch)")
                proc = type("Proc", (), {"returncode": 0})()  # fake success
            else:
                info(f"Force-fetching {task} from fork to ensure chain source is current")
                proc = run(["git", "fetch", push_url, f"+{task}:{task}"], check=False)
            if proc.returncode != 0:
                # If the local ref already exists, fall through and use
                # whatever's there; otherwise we can't chain at all.
                if not git_ok("show-ref", "--verify", "--quiet", f"refs/heads/{task}"):
                    warn(f"could not fetch {task} from fork and no local ref; "
                         f"later branches will chain from {full_commit[:9]} instead, "
                         f"which may re-surface conflicts you already resolved.")
                    port_cmd = base_port_cmd
                    continue
                warn(f"force-fetch of {task} failed; using stale local ref. "
                     f"If chaining surfaces conflicts you already resolved, "
                     f"delete the local {task} ref and rerun.")
            port_cmd = ["git", "cherry-pick", "--allow-empty", task]
            continue

        checkout_task_branch(task, upstream_remote, branch)

        if branch in args.accept_branch:
            info("Skipping code change due to -x option")
        else:
            # Always print the documented bash-shim entrypoint in the
            # rerun hint, regardless of whether the user invoked .sh or
            # .py -- the .sh wrapper just execs the .py, so following
            # the hint works in either case.
            rerun_hint = (
                f"==== .agents/scripts/backport-commit.sh -x {branch} "
                + " ".join(shlex.quote(a) for a in rerun_args)
            )
            try:
                applied = apply_cherry_pick(port_cmd, task, workspace, branch, rerun_hint)
            except ResumableConflict:
                return 2
            if not applied:
                # No-op cherry-pick (already on branch); nothing more to do.
                continue

        amend_message(full_commit, branch, pr_url)

        try:
            run_lint(workspace, upstream_remote, branch, task)
        except ResumableConflict:
            return 2

        # Compose body: amended commit's body becomes the PR body verbatim.
        new_subject = git("log", "-n1", "--format=%s", "HEAD")
        new_body = git("log", "-n1", "--format=%b", "HEAD")

        if args.preview:
            info("Preview Mode (no push, no PR)")
            info(f"Workspace: {workspace}")
            info(f"Task branch: {task}")
            print("Would have run:")
            print(f"  git push -u {push_url} {task}")
            print(f"  gh pr create -R {gh_repo} -B {branch} -H {pr_head} "
                  f"-t {shlex.quote(new_subject)} -F -  (body from stdin):")
            print("----")
            print(new_body)
            print("----")
            if final_reviewers:
                print(f"  And then request reviewers: {final_reviewers}")
        else:
            new_url = push_and_create_pr(
                gh_repo=gh_repo, push_url=push_url, pr_head=pr_head,
                base=branch, task=task, title=new_subject, body=new_body,
            )
            new_pr_num_match = re.search(r"/pull/(\d+)$", new_url)
            if new_pr_num_match and final_reviewers:
                request_reviewers(gh_repo, int(new_pr_num_match.group(1)), final_reviewers)

        # Chain: next branch cherry-picks from this task branch's tip.
        port_cmd = ["git", "cherry-pick", "--allow-empty", task]

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except CmdError as e:
        # Mirror what the bash version did: print stderr/stdout and exit 1.
        if e.stderr:
            print(e.stderr.rstrip(), file=sys.stderr)
        if e.output:
            print(e.output.rstrip(), file=sys.stderr)
        fail(f"command failed: {shlex.join(e.cmd)} (rc={e.returncode})")
    except KeyboardInterrupt:
        sys.exit(130)
