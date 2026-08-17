#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper instead.
#
# Wired into .cursor/hooks.json (beforeShellExecution) and .claude/settings.json
# (PreToolUse/Bash). Both scope it to push commands themselves, so reaching this
# script means deny; --format only picks the reply shape.
#
# Claude's `permissions.deny` is what actually blocks the push there; this only
# replaces a bare denial with a message. Its `if` stays narrow -- the broader
# `Bash(git * push *)` also matches .agents/scripts/git-push.sh and an `if`
# filter has no allow-list to exempt it -- so `git -C <dir> push` gets no
# message. Exit 2 on a bad flag fails closed in both harnesses.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

case "${1:-}" in
  --format=claude)
    printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
      "$msg"
    ;;
  --format=cursor)
    printf '{"permission":"deny","user_message":"%s","agent_message":"%s"}\n' "$msg" "$msg"
    ;;
  *)
    echo "usage: ${0##*/} --format=claude|cursor" >&2
    exit 2
    ;;
esac
