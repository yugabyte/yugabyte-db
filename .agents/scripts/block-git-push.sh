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
# Claude mode has to check the command itself, because its `if` filter is not
# a reliable gate on its own:
#   - `if` has no allow-list interplay, so a pattern broad enough to catch
#     `git -C <dir> push` also matches .agents/scripts/git-push.sh -- the very
#     helper this message recommends.
#   - `if` over-matches some shell syntax outright. `diff <(echo a) <(echo b)`,
#     which contains no push at all, triggers it.
# Without the check below, either case is denied with this message and the
# agent is stuck. `permissions.deny` is what actually blocks a real push, so
# staying silent here costs the message, never the enforcement.
#
# `if` is therefore kept to the narrow `Bash(git push *)`. The broader
# `Bash(git * push *)` stays in `permissions.deny`, so `git -C <dir> push` is
# still refused -- it just gets a bare denial rather than this message.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

if [[ "${1:-}" == "--format=claude" ]]; then
  # `git` at command position, immediately followed by `push`.
  python3 -c 'import json,sys
try:
    print((json.load(sys.stdin).get("tool_input") or {}).get("command") or "")
except Exception:
    pass' 2>/dev/null \
    | grep -Eq '(^|[[:space:];&|(])git[[:space:]]+push([[:space:];&|]|$)' \
    || exit 0

  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
    "$msg"
else
  printf '{"permission":"deny","user_message":"%s"}\n' "$msg"
fi
