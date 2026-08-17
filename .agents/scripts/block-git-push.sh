#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper to use instead.
#
# Registered for both agent harnesses, which take different response shapes:
#   Cursor       .cursor/hooks.json  beforeShellExecution -> {"permission": ...}
#   Claude Code  .claude/settings.json PreToolUse/Bash    -> hookSpecificOutput
#
# Pass --format=claude for the latter; the default is Cursor's shape.
#
# Cursor scopes this with its `matcher` regex and passes no command in, so
# reaching the script means the answer is deny.
#
# Claude mode has to check the command itself. Its `if` filters carry the same
# two patterns as `permissions.deny`, but an `if` filter is not equivalent to a
# deny rule and cannot be trusted alone:
#   - It has no allow-list interplay. `Bash(git * push *)` matches
#     .agents/scripts/git-push.sh -- the very helper this message recommends --
#     which the deny rule tolerates only because an explicit allow entry
#     outranks it.
#   - It over-matches some shell syntax outright. `diff <(echo a) <(echo b)`,
#     which contains no push at all, triggers it.
# Either case would otherwise be denied with this message and leave the agent
# stuck. `permissions.deny` is what actually blocks a real push, so staying
# silent here costs the message, never the enforcement.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

if [[ "${1:-}" == "--format=claude" ]]; then
  # `git` at command position, then `push` as its own word -- optionally with
  # global options in between, so `git -C <dir> push` and `git -c k=v push`
  # count. A path such as scripts/git-push.sh does not: its "git" is preceded
  # by "/" and its "push" by "-", so neither sits at a word boundary here.
  python3 -c 'import json,sys
try:
    print((json.load(sys.stdin).get("tool_input") or {}).get("command") or "")
except Exception:
    pass' 2>/dev/null \
    | grep -Eq '(^|[[:space:];&|(])git([[:space:]][^;&|]*)?[[:space:]]push([[:space:];&|]|$)' \
    || exit 0

  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
    "$msg"
else
  printf '{"permission":"deny","user_message":"%s"}\n' "$msg"
fi
