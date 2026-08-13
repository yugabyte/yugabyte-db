#!/usr/bin/env python3
"""block-upstream-push: agent shell hook that denies `git push` when the
resolved push target is the upstream repo's owner, while leaving pushes to
forks and unrelated remotes alone.

Unlike a pattern match on the command text, this resolves the remote to a URL
first, so `git push my-fork HEAD` succeeds and `git push origin master` does
not.

Serves both agent harnesses; the payload shape on stdin selects the dialect:

  Claude Code  PreToolUse/Bash hook, registered in .claude/settings.json.
               Command arrives as {"tool_input": {"command": ...}}. Replies
               with a `hookSpecificOutput.permissionDecision`, where staying
               silent means "no opinion" and normal permission rules apply.
  Cursor       beforeShellExecution hook, registered in .cursor/hooks.json.
               Command arrives as {"command": ...}. Replies with
               {"permission": ...}. That hook sets failClosed, so silence is
               not a safe "no opinion" -- an explicit allow is always emitted.

Claude Code has no failClosed equivalent: if this hook crashes or its config
goes stale, the push is allowed. .claude/settings.json therefore also keeps
`git push` on the `ask` list, so a human is prompted even when the hook does
not run. Cursor needs no such backstop.

Pass --format=claude|cursor to override the auto-detection (useful for tests).

Decisions:
  deny  -- target resolves to a blocked owner
  ask   -- command is a push but the target could not be resolved
  allow -- not a push, or the target is some other repo

Env overrides:
  GH_REPO         default: yugabyte/yugabyte-db (its owner is what gets blocked)
  BLOCKED_OWNERS  comma-separated owner list; overrides GH_REPO's owner
  BLOCKED_HOST    default: github.com
"""

import json
import os
import shlex
import subprocess
import sys

GH_REPO = os.environ.get("GH_REPO") or "yugabyte/yugabyte-db"
BLOCKED_HOST = (os.environ.get("BLOCKED_HOST") or "github.com").lower()
BLOCKED_OWNERS = [
    o.strip().lower()
    for o in (os.environ.get("BLOCKED_OWNERS") or GH_REPO.split("/")[0]).split(",")
    if o.strip()
]

OPERATORS = {"&&", "||", ";", "|", "&", "\n", "(", ")", "{", "}"}
# git's own options that take a separate value, so we can skip past them to the
# subcommand.
GIT_OPTS_WITH_VALUE = {"-C", "-c", "--git-dir", "--work-tree", "--namespace",
                       "--exec-path", "--super-prefix"}
# `git push` options that take a separate value, so a value is never mistaken
# for the remote name.
PUSH_OPTS_WITH_VALUE = {"-o", "--push-option", "--receive-pack", "--exec",
                        "--force-with-lease", "--repo"}


def decide(fmt, decision, reason=""):
    """Emit a decision in the harness's own dialect and exit.

    For Claude Code an `allow` is expressed by staying silent, so that normal
    permission rules still get a say. Cursor's hook runs failClosed, so it
    always gets an explicit verdict.
    """
    if fmt == "cursor":
        payload = {"permission": decision}
        if reason:
            # Cursor has used both spellings for the operator-facing string;
            # `permission` is what enforces, so send both and let it pick.
            payload.update(user_message=reason, userMessage=reason,
                           agentMessage=reason)
        json.dump(payload, sys.stdout)
        sys.stdout.write("\n")
    elif decision != "allow":
        json.dump({"hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,
            "permissionDecisionReason": reason,
        }}, sys.stdout)
        sys.stdout.write("\n")
    sys.exit(0)


def tokenize(cmd):
    """Split a shell command into tokens, keeping operators as their own tokens.

    Quote-aware: the `git push` inside `git commit -m "git push origin x"`
    stays a single non-operator token and so is never read as a command.
    """
    lexer = shlex.shlex(cmd, posix=True, punctuation_chars=True)
    lexer.whitespace_split = True
    return list(lexer)


def url_owner(url):
    """Return ("host", "owner") for a git URL, or None if unparseable."""
    if "://" in url:                       # scheme://[user@]host[:port]/owner/repo
        rest = url.split("://", 1)[1]
        rest = rest.split("@", 1)[-1]
        host, _, path = rest.partition("/")
        host = host.split(":", 1)[0]
    elif "@" in url and ":" in url:        # scp-like: user@host:owner/repo
        host = url.split("@", 1)[1].split(":", 1)[0]
        path = url.split(":", 1)[1]
    else:
        return None
    path = path.lstrip("/")
    if "/" not in path:
        return None
    return host.lower(), path.split("/", 1)[0].lower()


def git(workdir, *args):
    try:
        out = subprocess.run(["git", "-C", workdir, *args], capture_output=True,
                             text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return ""
    return out.stdout.strip() if out.returncode == 0 else ""


def find_push(tokens, workdir):
    """Scan tokens for a `git push`. Returns (found, remote_arg, workdir)."""
    at_command = True
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok in OPERATORS:
            at_command = True
            i += 1
            continue
        if not at_command:
            i += 1
            continue
        # Leading VAR=value assignments keep us at command position.
        if "=" in tok and not tok.startswith("-") and tok.split("=", 1)[0].isidentifier():
            i += 1
            continue

        argv0 = os.path.basename(tok)
        if argv0 == "cd" and i + 1 < len(tokens) and tokens[i + 1] not in OPERATORS:
            workdir = os.path.join(workdir, tokens[i + 1])
            at_command = False
            i += 2
            continue
        if argv0 != "git":
            at_command = False
            i += 1
            continue

        # Walk git's global options to reach the subcommand.
        i += 1
        while i < len(tokens) and tokens[i].startswith("-"):
            opt = tokens[i]
            if opt in GIT_OPTS_WITH_VALUE and i + 1 < len(tokens):
                if opt == "-C":
                    workdir = os.path.join(workdir, tokens[i + 1])
                i += 2
                continue
            if opt.startswith("--git-dir="):
                workdir = opt.split("=", 1)[1]
            i += 1
        if i >= len(tokens) or tokens[i] != "push":
            at_command = False
            continue

        # Found it; the first bare argument is the remote.
        i += 1
        remote = ""
        while i < len(tokens) and tokens[i] not in OPERATORS:
            tok = tokens[i]
            if tok.startswith("--repo="):
                remote = tok.split("=", 1)[1]
            elif tok in PUSH_OPTS_WITH_VALUE and i + 1 < len(tokens):
                if tok == "--repo":
                    remote = tokens[i + 1]
                i += 2
                continue
            elif not tok.startswith("-") and not remote:
                remote = tok
            i += 1
        return True, remote, workdir
    return False, "", workdir


def main():
    fmt = ""
    for arg in sys.argv[1:]:
        if arg.startswith("--format="):
            fmt = arg.split("=", 1)[1]

    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, UnicodeDecodeError):
        # No payload to inspect. Claude Code treats silence as "no opinion";
        # Cursor's hook is failClosed and will deny on its own.
        return
    if not fmt:
        fmt = "claude" if "tool_input" in payload else "cursor"

    # Claude Code nests the command under tool_input; Cursor puts it at top level.
    cmd = (payload.get("tool_input") or {}).get("command") or \
        payload.get("command") or ""
    # Cheap bail-out before any parsing: the overwhelming majority of commands.
    if "push" not in cmd:
        decide(fmt, "allow")

    workdir = payload.get("cwd") or ""
    if not workdir:
        roots = payload.get("workspace_roots") or []   # Cursor
        workdir = roots[0] if roots else ""
    if not workdir or not os.path.isdir(workdir):
        workdir = os.getcwd()

    try:
        tokens = tokenize(cmd)
    except ValueError:  # unbalanced quotes -- can't tell what this does
        decide(fmt, "ask", "Could not parse this command well enough to check "
                           "its push target. Confirm it does not push to an "
                           "upstream repo.")
    found, remote, workdir = find_push(tokens, workdir)
    if not found:
        decide(fmt, "allow")

    # No remote on the command line: whatever `git push` would pick by itself.
    if not remote:
        branch = git(workdir, "symbolic-ref", "--short", "-q", "HEAD")
        remote = (git(workdir, "config", "--get", f"branch.{branch}.remote")
                  or git(workdir, "config", "--get", "remote.pushDefault")
                  or "origin")

    # A remote name needs resolving; a URL is already the target. `--push` picks
    # up pushurl when one is configured.
    if "://" in remote or ("@" in remote and ":" in remote):
        url = remote
    else:
        url = git(workdir, "remote", "get-url", "--push", remote)
    if not url:
        decide(fmt, "ask", f"Could not resolve the push target for remote "
                           f"'{remote}' in {workdir}. Confirm manually that this "
                           f"is not a {BLOCKED_HOST}/{BLOCKED_OWNERS[0]} repo.")

    parsed = url_owner(url)
    if parsed is None:
        decide(fmt, "ask", f"Could not parse the push URL '{url}'. Confirm "
                           f"manually that this is not an upstream repo.")
    host, owner = parsed
    if host == BLOCKED_HOST and owner in BLOCKED_OWNERS:
        decide(fmt, "deny", f"Blocked: pushing to {host}/{owner} is not allowed "
                            f"(remote '{remote}' -> {url}). Use "
                            f".agents/scripts/git-push.sh to push to your fork "
                            f"instead.")
    decide(fmt, "allow")


if __name__ == "__main__":
    main()
