#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper to use instead.
#
# Registered for both agent harnesses, which take different response shapes:
#   Cursor       .cursor/hooks.json  beforeShellExecution -> {"permission": ...}
#   Claude Code  .claude/settings.json PreToolUse/Bash    -> hookSpecificOutput
#
# Pass --format=claude for the latter; the default is Cursor's shape.
#
# Neither caller passes the command in: each is scoped to push commands by its
# own config (Cursor's `matcher`, Claude's `if`), so reaching this script at
# all means the answer is "deny". On the Claude Code side `permissions.deny`
# is what actually blocks the push -- this hook only replaces a bare denial
# with a message saying where to go next.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

if [[ "${1:-}" == "--format=claude" ]]; then
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
    "$msg"
else
  printf '{"permission":"deny","user_message":"%s"}\n' "$msg"
fi
