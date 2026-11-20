#!/usr/bin/env python3
"""Backport a merged commit, or a merged PR stack, to one or more release branches.

Python rewrite of backport-commit.sh. Same CLI surface (flags, positional
arguments) and same exit-code contract:

    0  all branches succeeded; PR URLs printed on stdout
    1  pre-flight failure (bad args, dirty workspace, missing remote, ...)
    2  cherry-pick conflict OR lint failure on a task branch -- the user
       resolves / fixes lint, amends, and re-runs with `-x <branch>`
       (stack mode: re-runs the same command; no -x needed)
    6  stack mode: the PRs were opened, but `gh stack link` failed

Stack mode: given several commits (or `--stack <PR>`, which reads the merged
layers of a GitHub PR stack), each release branch gets a stacked PR, one
layer per commit, on `feature-stack/backport-<branch>-<feature>/<change>`
branches in the upstream repo -- GitHub cannot stack PRs from fork branches.
Every layer but the top gets `Jenkins: skip`: the top layer's build contains
the whole stack, so its one Jenkins run covers every layer.

Invoked via the bash shim `.agents/scripts/backport-commit.sh`.

Env overrides:
    YB_BACKPORT     workspace dir; default $HOME/code/backport-ybdb
                    (created as a git worktree off this script's repo,
                    sharing .git so fetches don't duplicate the object DB)
    YB_GH_REPO      upstream owner/name; default yugabyte/yugabyte-db
    YB_GH_FORK      <owner>/<repo> of the fork; default <gh-user>/<repo>.
                    Must be a fork of YB_GH_REPO, with any owner, including
                    the upstream org (e.g. yugabyte/yugabyte-db-dev).
    YB_FORK_URL     full URL of the fork; defaults to mirror upstream protocol.
                    Its path must name the YB_GH_FORK repo.
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
import urllib.parse
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
    return gh_cmd_json(["gh", "api", *args], check=check)


def gh_cmd_json(cmd, *, check=True):
    """Run a `gh` command whose stdout is JSON and parse it. Returns None on failure."""
    proc = run(cmd, check=False)
    if proc.returncode != 0:
        if check:
            raise CmdError(cmd, proc.returncode, proc.stdout, proc.stderr)
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


COMMIT_RE = re.compile(r"[0-9a-fA-F]{7,64}")


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="backport-commit",
        usage="%(prog)s [options] <commit> [<commit> ...] [<branch> ...]\n"
              "       %(prog)s [options] --stack <pr> [<branch> ...]",
        description="Backport a merged commit to one or more release branches. "
                    "Given several commits, or --stack, open a stacked PR on each "
                    "release branch with one layer per commit.",
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
    p.add_argument("--keep-workspace", action="store_true",
                   help="On success, don't delete local task branches or remove "
                        "the worktree (default cleans up after PRs are created).")
    p.add_argument("--stack", metavar="PR",
                   help="Backport the merged layers of the GitHub PR stack that "
                        "this PR (or stack number) belongs to, bottom first, as "
                        "a stacked PR on each branch.")
    p.add_argument("targets", nargs="*", metavar="commit-or-branch",
                   help="Commit SHAs (>=7 hex chars) in stack order, bottom "
                        "first, then destination branches.")
    args = p.parse_args(argv)

    # Release branch names (2025.2, 2.20) contain a dot, so they never pass
    # for a SHA.
    n = 0
    while n < len(args.targets) and COMMIT_RE.fullmatch(args.targets[n]):
        n += 1
    args.commits, args.branches = args.targets[:n], args.targets[n:]
    bad = [b for b in args.branches if COMMIT_RE.fullmatch(b)]
    if bad:
        p.error(f"commits must come before branches: {' '.join(bad)}")
    if args.stack and args.commits:
        p.error("pass either commits or --stack, not both")
    if not args.stack and not args.commits:
        p.error("no commit given (expected 7+ hex chars, or --stack <pr>)")
    if args.stack and not re.fullmatch(r"\d+", args.stack.lstrip("#")):
        p.error(f"--stack takes a PR or stack number, got {args.stack!r}")
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

def setup_workspace(workspace: Path, use_local: bool) -> bool:
    """Set up workspace and cd into it.

    For a fresh path, creates a git worktree off SCRIPT_REPO_ROOT (the
    repo containing this script). The worktree shares .git with the
    source repo, so the object database isn't duplicated and fetched
    refs are visible across worktrees. An existing path is reused
    as-is, whether it's a worktree, a clone, or a plain checkout --
    we trust it as long as it's a valid git working directory.

    Returns True iff this call created the worktree (so post-run cleanup
    can safely remove it without clobbering a pre-existing checkout).
    """
    created = False
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
        created = True

    os.chdir(workspace)
    return created


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


def originating_pr(gh_repo: str, commit: str) -> Optional[dict]:
    """Return the merge PR of $commit as the REST API's PR object, or None."""
    info(f"Looking up originating PR for {commit[:9]}")
    data = gh_json([f"repos/{gh_repo}/commits/{commit}/pulls"], check=False)
    if not data:
        print("No originating PR found; backport body will reference the commit hash only.")
        return None
    pr = data[0]
    print(f"Found originating PR: #{pr['number']} -> {pr['html_url']}")
    return pr


def lookup_originating_pr(gh_repo: str, commit: str) -> tuple[Optional[int], Optional[str]]:
    """Return (pr_number, pr_url) for the merge PR of $commit, or (None, None)."""
    pr = originating_pr(gh_repo, commit)
    if not pr:
        return None, None
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


def is_same_owner_fork(gh_repo: str, gh_fork: str) -> bool:
    """True iff gh_fork is owned by gh_repo's owner (e.g. yugabyte/yugabyte-db-dev).

    "<owner>:<branch>" can't name such a fork -- GitHub reads it as the
    upstream repo itself -- so its PRs need the REST head_repo field.
    """
    return gh_fork.split("/", 1)[0].lower() == gh_repo.split("/", 1)[0].lower()


def repo_from_url(url: str) -> Optional[str]:
    """Return "<owner>/<repo>" from a git remote URL, or None if it has no such path.

    Handles scheme URLs (https://, ssh://, with or without userinfo/port)
    and scp-like ones (git@host:owner/repo); a trailing ".git" is dropped.
    """
    if "://" in url:
        path = urllib.parse.urlsplit(url).path
    elif ":" in url:
        path = url.split(":", 1)[1]
    else:
        return None
    path = path.strip("/")
    if path.endswith(".git"):
        path = path[:-len(".git")]
    parts = path.split("/")
    if len(parts) != 2 or not all(parts):
        return None
    return path


def resolve_fork(gh_repo: str, gh_user: str, upstream_remote: str) -> tuple[str, str, str]:
    """Return (gh_fork, fork_owner, push_url). Aborts unless gh_fork is a fork of gh_repo."""
    upstream_owner = gh_repo.split("/", 1)[0]
    repo_name = gh_repo.split("/", 1)[1]

    gh_fork = os.environ.get("YB_GH_FORK") or ""
    if not gh_fork:
        if not gh_user or gh_user == upstream_owner:
            fail("no personal fork detected for the authenticated gh user. "
                 "Set YB_GH_FORK=<your-user>/<repo> or run 'gh repo fork' and re-run.")
        gh_fork = f"{gh_user}/{repo_name}"

    # Ask GitHub rather than compare names: the fork may share the upstream's
    # owner, and other spellings of the upstream (".git" suffix, a renamed
    # repo's old name) must not slip through as a push target.
    repo = gh_json([f"repos/{gh_fork}"], check=False) or {}
    if not repo.get("fork") or gh_repo.lower() not in {
            ((repo.get(k) or {}).get("full_name") or "").lower()
            for k in ("parent", "source")}:
        fail(f"'{gh_fork}' is not a fork of {gh_repo} (or could not be looked up). "
             f"Set YB_GH_FORK=<owner>/<repo> to a fork of {gh_repo}, or run "
             f"'gh repo fork {gh_repo}', and re-run. "
             f"Refusing to push to anything else.")
    gh_fork = repo["full_name"]
    fork_owner = gh_fork.split("/", 1)[0]

    info(f"Pushing to fork: {gh_fork} (PRs target {gh_repo})")

    # Build push URL: env override wins; otherwise mirror upstream's protocol.
    # The override must name the fork checked above, or it would bypass that
    # check, and the head_repo / existing-PR lookups would name a repo the
    # branch was never pushed to.
    push_url = os.environ.get("YB_FORK_URL")
    if push_url:
        url_repo = repo_from_url(push_url)
        if (url_repo or "").lower() != gh_fork.lower():
            # Don't echo the URL: it may carry a token in its userinfo.
            fail(f"YB_FORK_URL names '{url_repo or '<no owner/repo path>'}', "
                 f"not the fork {gh_fork}. Point it at {gh_fork}, or unset it "
                 f"to derive the URL.")
    else:
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


def existing_pr_url(gh_repo: str, gh_fork: str, task: str) -> Optional[str]:
    """If a PR from gh_fork's branch $task already exists, return its URL.

    Looks PRs up by branch name and matches the head repo, rather than
    using the REST "head=<owner>:<branch>" filter: for a fork owned by the
    upstream's owner, that label is ambiguous with the upstream repo itself.
    Aborts if the lookup fails: reading that as "no PR" would push and try
    to open a PR that may already exist.
    """
    # gh before 2.89 returns headRepository as {id, name} only, without
    # nameWithOwner, so the owner comes from headRepositoryOwner.
    prs = gh_cmd_json(["gh", "pr", "list", "-R", gh_repo, "--head", task,
                       "--state", "all",
                       "--json", "url,headRepository,headRepositoryOwner"],
                      check=False)
    if not isinstance(prs, list):
        fail(f"could not look up existing PRs for {task} on {gh_repo}. "
             f"Check 'gh auth status' and the network, then re-run.")
    for pr in prs:
        pr = pr or {}
        owner = (pr.get("headRepositoryOwner") or {}).get("login") or ""
        name = (pr.get("headRepository") or {}).get("name") or ""
        if f"{owner}/{name}".lower() == gh_fork.lower():
            return pr.get("url")
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
    # footers, with `Original commit: <SHA> / #<PR>` (GitHub-merged) or
    # `Original commit: <SHA> / D<diff>` (Phorge-merged) inserted above the
    # test-plan heading -- either Markdown `## Test plan` or Phorge
    # `Test Plan:` -- or appended at the end, above any trailers, if no
    # heading exists.
    raw_body = git("log", "-n1", "--format=%b", commit)
    body_lines = [
        line for line in raw_body.splitlines()
        if not (line.startswith("Backport of:")
                or line.startswith("Original PR:")
                or line.startswith("Original commit:"))
    ]
    pr_num_match = re.search(r"/pull/(\d+)", pr_url) if pr_url else None
    # Phorge-landed commits leave `Differential Revision: <url>/D<num>` in the
    # body. Use the diff number when no GitHub PR was found.
    phorge_match = (re.search(r"Differential Revision:\s*\S*?/D(\d+)", raw_body)
                    if not pr_num_match else None)
    if pr_num_match:
        original_line = f"Original commit: {commit} / #{pr_num_match.group(1)}"
    elif phorge_match:
        original_line = f"Original commit: {commit} / D{phorge_match.group(1)}"
    else:
        original_line = f"Original commit: {commit}"

    def is_test_plan_heading(line: str) -> bool:
        return line.startswith("## Test plan") or line.startswith("Test Plan:")

    # Single pass: insert above the first test-plan heading. If none is
    # found (rare -- nearly every commit body has one), fall through to
    # the append-at-end branch.
    new_body_lines: list[str] = []
    inserted = False
    for line in body_lines:
        if not inserted and is_test_plan_heading(line):
            new_body_lines.append(original_line)
            new_body_lines.append("")
            inserted = True
        new_body_lines.append(line)
    if not inserted:
        while new_body_lines and not new_body_lines[-1].strip():
            new_body_lines.pop()
        # Git reads trailers only from the last paragraph, so a line after
        # `Co-authored-by:` would drop that co-author from the backport.
        if ends_with_trailers(new_body_lines):
            blanks = [i for i, line in enumerate(new_body_lines)
                      if not line.strip()]
            last_paragraph = blanks[-1] + 1 if blanks else 0
            new_body_lines[last_paragraph:last_paragraph] = [original_line, ""]
        else:
            if new_body_lines:
                new_body_lines.append("")
            new_body_lines.append(original_line)
    new_body = "\n".join(new_body_lines)

    run(["git", "commit", "--amend", "--cleanup=verbatim",
         "-m", new_subject, "-m", new_body])


def ends_with_trailers(body_lines: list[str]) -> bool:
    message = "Subject\n\n" + "\n".join(body_lines) + "\n"
    return bool(run(["git", "interpret-trailers", "--parse", "--no-divider"],
                    input_=message).stdout.strip())


def run_lint(workspace: Path, upstream_remote: str, branch: str, task: str,
             base_rev: Optional[str] = None, stack_mode: bool = False) -> None:
    """Run the workspace's own lint.sh, with whatever flag it supports.

    Lints against $base_rev, default $upstream_remote/$branch; a stack layer
    passes the layer below it.

    lint.sh on master and on the active release branches (2024.2 and up) is
    a thin wrapper around lint.py that takes `--rev <base>`. Older branches
    ship a cpplint runner that takes `-c` / `--changed-only` instead, so if
    `--rev` isn't recognized we fall back to `-c`.

    Raises ResumableConflict on lint failure.
    """
    bp_lint = workspace / "build-support" / "lint.sh"
    if not bp_lint.is_file() or not os.access(bp_lint, os.X_OK):
        warn(f"{bp_lint} not found or not executable; skipping lint check.")
        return

    base_rev = base_rev or f"{upstream_remote}/{branch}"
    info(f"Running linter on {task} (vs {base_rev})")
    # LC_ALL=C keeps the "unknown flag: --rev" probe locale-independent
    # so the fallback path triggers on any system, not just English ones.
    lint_env = {**os.environ, "LC_ALL": "C"}
    proc = run([str(bp_lint), "--rev", base_rev],
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
        if stack_mode:
            info("Then re-run the same command; it resumes from this layer.")
        else:
            info(f"Then re-run with -x {branch} to publish.")
        info("")
        info("(The task branch is unpublished, so amending is safe -- and")
        info("required: each task branch must be a single commit so the")
        info("chain logic at the bottom of the loop, which cherry-picks the")
        info("TIP of $task_branch onto the next branch, picks up the full")
        info("change. The src/AGENTS.md \"never amend\" rule applies to public")
        info("commits, not to in-flight task branches.)")
        raise ResumableConflict()


def push_and_create_pr(*, gh_repo: str, gh_fork: str, push_url: str, pr_head: str,
                       base: str, task: str, title: str, body: str) -> str:
    """Push the task branch and open the PR. Returns PR URL."""
    info(f"Publishing backport PR for branch {task}")
    run(["git", "push", "-u", push_url, task])
    if is_same_owner_fork(gh_repo, gh_fork):
        # head_repo must be the full "<owner>/<repo>": GitHub's docs call it
        # the repository's name, but the bare name gets 422 "field head is
        # invalid". maintainer_can_modify defaults to true, which only an
        # actor with push access to the fork may grant (else 422
        # "fork_collab"), e.g. not a bot token that can only read the fork;
        # the org owns the fork, so the grant adds nothing.
        proc = run(
            ["gh", "api", "-X", "POST", f"repos/{gh_repo}/pulls",
             "--jq", ".html_url", "--input", "-"],
            input_=json.dumps({"title": title, "body": body, "base": base,
                               "head": task, "head_repo": gh_fork,
                               "maintainer_can_modify": False}),
        )
    else:
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


# --- Stack mode -------------------------------------------------------------

STACK_BRANCH_RE = re.compile(r"feature-stack/([^/]+)/([^/]+)")


class Layer:
    """One commit of a stack backport, with what its originating PR says."""
    def __init__(self, commit: str, pr: Optional[dict], reviewers: str):
        self.commit = commit
        self.pr_url = pr["html_url"] if pr else None
        self.reviewers = reviewers
        m = STACK_BRANCH_RE.fullmatch(((pr or {}).get("head") or {}).get("ref") or "")
        self.feature = m.group(1) if m else None
        self.change = m.group(2) if m else None


def stack_layer_pr(gh_repo: str, task: str) -> Optional[dict]:
    """Return the upstream PR from branch $task, preferring an open one.

    The dict has url, number, state (OPEN / MERGED / CLOSED), isDraft and
    mergeCommit. Aborts if the lookup fails, as existing_pr_url does.
    """
    prs = gh_cmd_json(["gh", "pr", "list", "-R", gh_repo, "--head", task,
                       "--state", "all",
                       "--json", "url,number,state,isDraft,mergeCommit,"
                                 "headRepository,headRepositoryOwner"],
                      check=False)
    if not isinstance(prs, list):
        fail(f"could not look up existing PRs for {task} on {gh_repo}. "
             f"Check 'gh auth status' and the network, then re-run.")
    mine = [pr for pr in prs
            if f"{(pr.get('headRepositoryOwner') or {}).get('login', '')}/"
               f"{(pr.get('headRepository') or {}).get('name', '')}".lower()
            == gh_repo.lower()]
    order = {"OPEN": 0, "MERGED": 1, "CLOSED": 2}
    mine.sort(key=lambda pr: order.get(pr.get("state"), 3))
    return mine[0] if mine else None


def resolve_stack_commits(gh_repo: str, ref: str) -> list[str]:
    """Return the merge commits of the merged layers of a GitHub PR stack.

    $ref is a stack number or the number of a PR in the stack; the two
    never collide. Layers come bottom first. Unmerged layers, always the top
    of the stack, are left out with a warning.
    """
    ref = ref.lstrip("#")
    stack = gh_json([f"repos/{gh_repo}/stacks/{ref}"], check=False)
    if not (stack or {}).get("pull_requests"):
        pr = gh_json([f"repos/{gh_repo}/pulls/{ref}"], check=False) or {}
        number = (pr.get("stack") or {}).get("number")
        if not number:
            fail(f"#{ref} is neither a PR stack nor a PR in one on {gh_repo}.")
        stack = gh_json([f"repos/{gh_repo}/stacks/{number}"], check=False) or {}
    info(f"Reading PR stack {stack.get('html_url') or stack.get('number')}")

    commits: list[str] = []
    unmerged: list[str] = []
    for pr in stack.get("pull_requests") or []:
        if not pr.get("merged_at"):
            unmerged.append(f"#{pr['number']}")
            continue
        if unmerged:
            fail(f"#{pr['number']} is merged, but {', '.join(unmerged)} below "
                 f"it is not; backport the commits explicitly instead.")
        # The stack listing carries no merge commit; the PR does.
        sha = (gh_json([f"repos/{gh_repo}/pulls/{pr['number']}"]) or {}).get(
            "merge_commit_sha")
        if not sha:
            fail(f"could not read the merge commit of #{pr['number']}.")
        print(f"  #{pr['number']} -> {sha[:9]} {pr.get('title', '')}")
        commits.append(sha)
    if unmerged:
        warn(f"leaving out unmerged layers: {', '.join(unmerged)}")
    if not commits:
        fail(f"no layer of the stack is merged yet; nothing to backport.")
    return commits


def stack_task_names(layers: list[Layer], branch: str) -> list[str]:
    """Return the feature-stack/<feature>/<change> branch of each layer on $branch.

    Reuses the original stack's names when every commit came from one; the
    feature gets a `backport-<branch>-` prefix so each release branch has its
    own stack.
    """
    features = {layer.feature for layer in layers}
    if len(features) == 1 and None not in features:
        feature = f"backport-{branch}-{features.pop()}"
    else:
        feature = f"backport-{branch}-{layers[0].commit[:9]}"
    names: list[str] = []
    seen: set[str] = set()
    for layer in layers:
        change = layer.change or layer.commit[:9]
        if change in seen:
            change = f"{change}-{layer.commit[:9]}"
        seen.add(change)
        names.append(f"feature-stack/{feature}/{change}")
    return names


def cherry_pick_cmd(source: str) -> list[str]:
    """`git cherry-pick` of $source, with -m 1 if it is a merge commit."""
    if len(git("rev-list", "--parents", "-n1", source).split()) > 2:
        return ["git", "cherry-pick", "-m", "1", "--allow-empty", source]
    return ["git", "cherry-pick", "--allow-empty", source]


def prepare_stack(*, gh_repo: str, upstream_remote: str, workspace: Path,
                  branch: str, layers: list[Layer], tasks: list[str],
                  chain_src: list[Optional[str]],
                  rerun_hint: str) -> list[tuple[Layer, str, Optional[dict]]]:
    """Build one task branch per layer on $branch, each on the one below.

    Returns (layer, task, existing open PR or None) for each layer kept; a
    layer whose change $branch already has, or whose backport PR merged, is
    dropped. Resumable: a layer whose task branch already holds its one
    commit (a conflict the user resolved) is kept as is, even if $branch
    moved on since, and a layer with an open PR is fetched from upstream.
    Updates $chain_src[i] to the commit the next branch should cherry-pick
    for layer i. Raises ResumableConflict on a conflict or a lint failure.
    """
    parent = f"{upstream_remote}/{branch}"
    kept: list[tuple[Layer, str, Optional[dict]]] = []
    for i, (layer, task) in enumerate(zip(layers, tasks)):
        print(f"---- layer {i + 1}/{len(layers)}: {task}")
        if git_ok("merge-base", "--is-ancestor", layer.commit, f"{upstream_remote}/{branch}"):
            info(f"{branch} already contains {layer.commit[:9]}; dropping this layer.")
            chain_src[i] = None
            continue
        existing = stack_layer_pr(gh_repo, task)
        if existing and existing["state"] == "MERGED":
            # Its squash commit on $branch is the resolved change: chain
            # from it.
            info(f"{existing['url']} already merged this layer into {branch}; "
                 f"dropping it.")
            chain_src[i] = (existing.get("mergeCommit") or {}).get("oid")
            continue
        if existing and existing["state"] == "CLOSED":
            fail(f"{existing['url']}, the PR for {task}, is closed. Reopen it "
                 f"(gh pr reopen {existing['number']} -R {gh_repo}) and re-run.")
        if existing:
            info(f"Existing backport PR found: {existing['url']} -- keeping it")
            if git("symbolic-ref", "--short", "HEAD", check=False) != task:
                proc = run(["git", "fetch", upstream_remote, f"+{task}:{task}"],
                           check=False)
                if proc.returncode != 0:
                    if not git_ok("show-ref", "--verify", "--quiet", f"refs/heads/{task}"):
                        fail(f"could not fetch {task} from {upstream_remote}, and "
                             f"there is no local {task}:\n{proc.stderr.strip()}")
                    warn(f"could not fetch {task}; using the local {task}.")
            kept.append((layer, task, existing))
            parent = task
            chain_src[i] = task
            continue

        if git_ok("show-ref", "--verify", "--quiet", f"refs/heads/{task}"):
            # $parent may have moved on since the last run (the release
            # branch took other backports), so accept a layer on any commit
            # $parent already contains.
            ahead = int(git("rev-list", "--count", f"{parent}..{task}"))
            if ahead > 1:
                fail(f"{task} has {ahead} commits on top of {parent}; a layer "
                     f"must be one commit. Squash them and re-run.")
            base = f"{task}^" if ahead else task
            if not git_ok("merge-base", "--is-ancestor", base, parent):
                fail(f"{task} is not based on {parent}. Delete it "
                     f"('git branch -D {task}') and re-run.")
            if ahead:
                info(f"Checking out task branch: {task}")
                run(["git", "checkout", task])
            else:
                info(f"Resetting task branch {task} to {parent}")
                run(["git", "checkout", "-B", task, parent])
        else:
            info(f"Creating task branch: {task} (on {parent})")
            run(["git", "checkout", "-b", task, parent])
            ahead = 0

        if ahead == 0:
            port_cmd = cherry_pick_cmd(chain_src[i] or layer.commit)
            if not apply_cherry_pick(port_cmd, task, workspace, branch, rerun_hint):
                run(["git", "checkout", "--detach"])
                run(["git", "branch", "-D", task])
                chain_src[i] = None
                continue
        else:
            info(f"{task} already holds its commit; using it as is")

        # Amend only once: a layer above may already be built on this commit.
        if not git("log", "-n1", "--format=%s", "HEAD").startswith(f"[BACKPORT {branch}]"):
            amend_message(layer.commit, branch, layer.pr_url)
        run_lint(workspace, upstream_remote, branch, task,
                 base_rev=git("rev-parse", "HEAD^"), stack_mode=True)
        kept.append((layer, task, None))
        parent = task
        chain_src[i] = task
    return kept


def publish_stack(*, gh_repo: str, upstream_remote: str, branch: str,
                  kept: list[tuple[Layer, str, Optional[dict]]],
                  reviewers_override: Optional[str], preview: bool) -> int:
    """Push the new layers to upstream, open their PRs bottom up, and link them.

    Mirrors create-pr.sh: a layer with a PR below it opens as a draft and is
    marked ready once linked, so the CI that readiness starts already sees a
    stack member. A draft left by an earlier run is marked ready too.
    Returns 0, or 6 if `gh stack link` failed.
    """
    new = [(i, layer, task) for i, (layer, task, pr) in enumerate(kept) if not pr]

    def pr_body(i: int, task: str) -> str:
        body = git("log", "-n1", "--format=%b", task)
        if i < len(kept) - 1:
            body = f"{body.rstrip()}\n\nJenkins: skip\n"
        return body
    if preview:
        info("Preview Mode (no push, no PR)")
        print("Would have run:")
        if new:
            print(f"  git push {upstream_remote} "
                  + " ".join(f"{t}:refs/heads/{t}" for _, _, t in new))
        for i, layer, task in new:
            base = branch if i == 0 else kept[i - 1][1]
            subject = git("log", "-n1", "--format=%s", task)
            draft = " --draft" if i > 0 else ""
            skip = " + Jenkins: skip" if i < len(kept) - 1 else ""
            print(f"  gh pr create -R {gh_repo} -B {base} -H {task}{draft} "
                  f"-t {shlex.quote(subject)} -F -  (body: the commit body{skip})")
            reviewers = (reviewers_override if reviewers_override is not None
                         else layer.reviewers)
            if reviewers:
                print(f"    and request reviewers: {reviewers}")
        if len(kept) > 1:
            print(f"  gh stack link --remote {upstream_remote} --base {branch} "
                  f"<the {len(kept)} PRs, bottom first>")
        return 0

    if new:
        info(f"Pushing {len(new)} layer(s) to {upstream_remote}")
        run(["git", "push", upstream_remote,
             *[f"{t}:refs/heads/{t}" for _, _, t in new]])

    pr_nums: list[int] = []
    drafts: list[int] = []
    for i, (layer, task, pr) in enumerate(kept):
        if pr:
            pr_nums.append(pr["number"])
            if i > 0 and pr.get("isDraft"):
                drafts.append(pr["number"])
            continue
        base = branch if i == 0 else kept[i - 1][1]
        subject = git("log", "-n1", "--format=%s", task)
        body = pr_body(i, task)
        info(f"Opening PR for {task} (base {base})")
        cmd = ["gh", "pr", "create", "-R", gh_repo, "-B", base, "-H", task,
               "-t", subject, "-F", "-"]
        if i > 0:
            cmd.append("--draft")
        url = run(cmd, input_=body).stdout.strip()
        print(url)
        num = int(url.rsplit("/", 1)[1])
        pr_nums.append(num)
        if i > 0:
            drafts.append(num)
        reviewers = (reviewers_override if reviewers_override is not None
                     else layer.reviewers)
        request_reviewers(gh_repo, num, reviewers)

    if len(pr_nums) < 2:
        return 0
    nums = [str(n) for n in pr_nums]
    info(f"gh stack link --base {branch} {' '.join(nums)}")
    proc = run(["gh", "stack", "link", "--remote", upstream_remote,
                "--base", branch, *nums], check=False)
    if proc.returncode != 0:
        print((proc.stderr or proc.stdout).rstrip(), file=sys.stderr)
        info(f"Backport ERROR: the PRs are open, but linking them into a stack failed.")
        info(f"Fix the cause above, then run:")
        info(f"  gh stack link --remote {upstream_remote} --base {branch} {' '.join(nums)}")
        if drafts:
            info(f"  gh pr ready -R {gh_repo} <pr>   for each of: "
                 f"{' '.join(map(str, drafts))}")
        return 6
    for num in drafts:
        run(["gh", "pr", "ready", str(num), "-R", gh_repo])
    return 0


def main_stack(args, *, gh_repo: str, gh_user: str, upstream_remote: str,
               workspace: Path, worktree_created: bool,
               branches: list[str], interactive_prompt: bool) -> int:
    """Backport several commits as a stacked PR on each release branch."""
    if not args.preview and run(["gh", "stack", "--help"], check=False).returncode != 0:
        fail("stack backports need the gh-stack extension: "
             "gh extension install github/gh-stack")
    if args.accept_branch:
        info("Ignoring -x: stack mode resumes from the task branches it finds.")

    commits = (resolve_stack_commits(gh_repo, args.stack) if args.stack
               else args.commits)
    commits = [ensure_commit_local(c, upstream_remote) for c in commits]
    if len(set(commits)) != len(commits):
        fail("the same commit is listed twice.")

    explicit_rev = normalize_reviewers(args.reviewers)
    layers: list[Layer] = []
    for commit in commits:
        pr = originating_pr(gh_repo, commit)
        reviewers = ""
        if pr and explicit_rev is None:
            reviewers = detect_auto_reviewers(gh_repo, pr["number"], gh_user)
            if reviewers:
                print(f"Auto-detected reviewers from PR #{pr['number']}: {reviewers}")
        layers.append(Layer(commit, pr, reviewers))

    rerun_args: list[str] = []
    if args.preview:    rerun_args.append("-n")
    if args.local:      rerun_args.append("-l")
    if explicit_rev is not None:
        rerun_args.extend(["-r", explicit_rev])
    # Keep --stack: one merged layer as a bare SHA would rerun in
    # single-commit mode.
    rerun_args.extend(["--stack", args.stack] if args.stack else commits)
    rerun_args.extend(branches)
    rerun_hint = (".agents/scripts/backport-commit.sh "
                  + " ".join(shlex.quote(a) for a in rerun_args))

    # chain_src[i]: what the next branch cherry-picks for layer i -- the
    # previous branch's resolved layer, so its conflict resolutions carry
    # forward, or None for the original commit.
    chain_src: list[Optional[str]] = [None] * len(layers)
    task_branches: list[str] = []
    for branch in branches:
        if branch == "master":
            continue
        print()
        print(f"================== Release Branch {branch}")
        if interactive_prompt and sys.stdin.isatty():
            ans = input(f"Backport the {len(layers)}-commit stack to branch "
                        f"{branch} [Y/n]? ").strip()
            if ans.lower().startswith("n"):
                continue
        elif interactive_prompt:
            fail("auto-discovery in non-interactive mode; pass branches explicitly.")

        tasks = stack_task_names(layers, branch)
        try:
            kept = prepare_stack(
                gh_repo=gh_repo, upstream_remote=upstream_remote,
                workspace=workspace, branch=branch, layers=layers, tasks=tasks,
                chain_src=chain_src, rerun_hint=rerun_hint)
        except ResumableConflict:
            return 2
        if not kept:
            info(f"{branch} already has every commit of the stack; skipping.")
            continue
        rc = publish_stack(gh_repo=gh_repo, upstream_remote=upstream_remote,
                           branch=branch, kept=kept,
                           reviewers_override=explicit_rev, preview=args.preview)
        if rc:
            return rc
        task_branches.extend(task for _, task, _ in kept)

    if not args.preview and not args.keep_workspace:
        cleanup_after_success(
            workspace=workspace,
            source_repo=SCRIPT_REPO_ROOT,
            task_branches=task_branches,
            try_remove_worktree=worktree_created and not args.local,
        )
    return 0


# --- Post-run cleanup -------------------------------------------------------

def cleanup_after_success(workspace: Path, source_repo: Path,
                          task_branches: list[str],
                          try_remove_worktree: bool) -> None:
    """Delete local task branches and (optionally) remove the worktree.

    Called only on the success path (return 0 from main). Task branches
    are no longer needed locally once their PRs are open -- the fork
    holds the canonical copies. The worktree is only removed if we
    created it this run AND it's still a linked worktree of source_repo;
    that protects a plain checkout someone aimed YB_BACKPORT at.
    """
    if task_branches:
        info(f"Deleting local task branches: {', '.join(task_branches)}")
        # If HEAD points at one of the branches we're about to delete,
        # detach so `git branch -D` doesn't refuse on "checked out here".
        current = git("symbolic-ref", "--short", "HEAD", check=False)
        if current in task_branches:
            run(["git", "checkout", "--detach"], check=False)
        for task in task_branches:
            proc = run(["git", "branch", "-D", task], check=False)
            if proc.returncode != 0:
                warn(f"failed to delete local branch {task}: "
                     f"{(proc.stderr or proc.stdout).strip()}")

    if not try_remove_worktree:
        return
    # A linked worktree has `.git` as a file pointing at the source repo's
    # .git/worktrees/<name> dir; a plain clone has `.git` as a directory.
    # Refuse to remove the latter -- the user pointed YB_BACKPORT at it.
    if not (workspace / ".git").is_file():
        info(f"Workspace is not a linked worktree; leaving in place: {workspace}")
        return
    info(f"Removing worktree: {workspace}")
    # cd out of the worktree before asking git to remove it.
    os.chdir(source_repo)
    proc = run(["git", "-C", str(source_repo), "worktree", "remove",
                "--force", str(workspace)], check=False)
    if proc.returncode != 0:
        warn(f"failed to remove worktree {workspace}: "
             f"{(proc.stderr or proc.stdout).strip()}")


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


def choose_branches(args, upstream_remote: str) -> tuple[list[str], bool]:
    """Return (destination branches, whether to confirm each interactively)."""
    interactive_prompt = False
    branches = list(args.branches)
    if not branches:
        branches = detect_stable_branches(upstream_remote, args.stable_limit)
        if not args.stable_limit:
            interactive_prompt = True
    if not branches:
        fail("no destination branches given and auto-discovery returned none.")
    return branches, interactive_prompt


def skip_leftover_cherry_pick() -> None:
    """Clean up an empty cherry-pick left over from a prior run."""
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
    worktree_created = setup_workspace(workspace, args.local)
    upstream_remote = detect_upstream_remote(gh_repo)
    info(f"Fetching latest code from {upstream_remote}")
    run(["git", "fetch", upstream_remote])

    gh_user_proc = run(["gh", "api", "user", "--jq", ".login"], check=False)
    gh_user = gh_user_proc.stdout.strip() if gh_user_proc.returncode == 0 else ""

    if args.stack or len(args.commits) > 1:
        # Stack branches live upstream, so no fork is involved.
        check_clean_tree()
        branches, interactive_prompt = choose_branches(args, upstream_remote)
        skip_leftover_cherry_pick()
        return main_stack(args, gh_repo=gh_repo, gh_user=gh_user,
                          upstream_remote=upstream_remote, workspace=workspace,
                          worktree_created=worktree_created, branches=branches,
                          interactive_prompt=interactive_prompt)
    args.commit = args.commits[0]

    # Build push URL (needs upstream remote).
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

    branches, interactive_prompt = choose_branches(args, upstream_remote)
    skip_leftover_cherry_pick()

    # Per-branch loop. The chain logic: after each successful branch the next
    # one cherry-picks from the previous task branch's tip (so any conflict
    # resolution carries forward).
    port_cmd = base_port_cmd
    # Task branches that have a published (or pre-existing) backport PR --
    # candidates for local cleanup on success.
    published_tasks: list[str] = []
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
        existing = existing_pr_url(gh_repo, gh_fork, task)
        if existing:
            info(f"Existing backport PR found: {existing} -- skipping")
            published_tasks.append(task)
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
            if is_same_owner_fork(gh_repo, gh_fork):
                print(f"  gh api -X POST repos/{gh_repo}/pulls  (head={task}, "
                      f"head_repo={gh_fork}, maintainer_can_modify=false, base={branch}, "
                      f"title={shlex.quote(new_subject)}, body:)")
            else:
                print(f"  gh pr create -R {gh_repo} -B {branch} -H {pr_head} "
                      f"-t {shlex.quote(new_subject)} -F -  (body from stdin):")
            print("----")
            print(new_body)
            print("----")
            if final_reviewers:
                print(f"  And then request reviewers: {final_reviewers}")
        else:
            new_url = push_and_create_pr(
                gh_repo=gh_repo, gh_fork=gh_fork, push_url=push_url, pr_head=pr_head,
                base=branch, task=task, title=new_subject, body=new_body,
            )
            new_pr_num_match = re.search(r"/pull/(\d+)$", new_url)
            if new_pr_num_match and final_reviewers:
                request_reviewers(gh_repo, int(new_pr_num_match.group(1)), final_reviewers)
            published_tasks.append(task)

        # Chain: next branch cherry-picks from this task branch's tip.
        port_cmd = ["git", "cherry-pick", "--allow-empty", task]

    if not args.preview and not args.keep_workspace:
        cleanup_after_success(
            workspace=workspace,
            source_repo=SCRIPT_REPO_ROOT,
            task_branches=published_tasks,
            try_remove_worktree=worktree_created and not args.local,
        )

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
