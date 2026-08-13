#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper to use instead.
#
# Registered for both agent harnesses, which take different response shapes:
#   Cursor       .cursor/hooks.json  beforeShellExecution -> {"permission": ...}
#   Claude Code  .claude/settings.json PreToolUse/Bash    -> hookSpecificOutput
#
# Pass --format=claude for the latter; the default is Cursor's shape.
#
# Cursor scopes this to push commands with its `matcher` regex and passes no
# command in, so reaching the script means the answer is deny.
#
# Claude Code needs a second check. Its `if` filter uses permission-rule
# syntax, and `Bash(git * push *)` also matches paths that merely contain
# "git" and "push" -- including .agents/scripts/git-push.sh, the helper this
# very message recommends. The deny rule tolerates that because an explicit
# allow entry outranks it; an `if` filter has no such interplay. So in Claude
# mode, confirm the command really invokes `git ... push` before answering.
#
# Note `permissions.deny` is what actually blocks the push here; this hook
# only replaces a bare denial with a message saying where to go next. Staying
# silent therefore fails safe -- the push is still refused, just tersely.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

if [[ "${1:-}" == "--format=claude" ]]; then
  cmd=$(python3 -c 'import json,sys
try:
    print((json.load(sys.stdin).get("tool_input") or {}).get("command") or "")
except Exception:
    pass' 2>/dev/null)

  # `git` as a word at command position, and whitespace before `push` -- so a
  # path like scripts/git-push.sh does not qualify.
  printf '%s' "$cmd" \
    | grep -Eq '(^|[[:space:];&|(])git[[:space:]]([^;&|]*[[:space:]])?push([[:space:]]|$)' \
    || exit 0

  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
    "$msg"
else
  printf '{"permission":"deny","user_message":"%s"}\n' "$msg"
fi
