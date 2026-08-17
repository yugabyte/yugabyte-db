#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper to use instead.
#
# Registered for both agent harnesses, which take different response shapes:
#   Cursor       .cursor/hooks.json  beforeShellExecution -> {"permission": ...}
#   Claude Code  .claude/settings.json PreToolUse/Bash    -> hookSpecificOutput
#
# Pass --format=claude for the latter; the default is Cursor's shape.
#
# Takes no input and always denies. Each harness scopes it to push commands
# with its own config -- Cursor's `matcher`, Claude's `if` -- so reaching this
# script at all means the answer is deny.
#
# Claude's `if` is kept to the narrow `Bash(git push *)`. The broader
# `Bash(git * push *)` that sits in `permissions.deny` also matches
# .agents/scripts/git-push.sh -- the helper this message recommends -- and an
# `if` filter has no allow-list to exempt it the way a deny rule does. Since
# this script no longer inspects the command, `if` is the only gate, so it has
# to stay narrow. Consequence: `git -C <dir> push` is still refused by the
# deny rule, it just gets a bare denial rather than this message.
#
# Note `permissions.deny` is what actually blocks the push on the Claude side.
# This hook only replaces a bare denial with a message saying where to go next.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

if [[ "${1:-}" == "--format=claude" ]]; then
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
    "$msg"
else
  printf '{"permission":"deny","user_message":"%s"}\n' "$msg"
fi
