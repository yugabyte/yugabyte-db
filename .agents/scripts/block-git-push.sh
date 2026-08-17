#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper instead.

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
