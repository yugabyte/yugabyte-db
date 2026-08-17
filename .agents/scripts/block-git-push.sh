#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper instead.
#
# Claude's PreToolUse `matcher` is matched against the tool name only, so this
# runs on every Bash call and filters the command itself. The alternative, an
# `if` filter, over-matches: `diff <(echo a) <(echo b)` trips `Bash(git push *)`
# and would be refused with a push message. Cursor scopes by command regex and
# passes no command in, so reaching its branch already means deny.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh to push to your fork instead.'

case "${1:-}" in
  --format=claude)
    payload=$(cat)
    # Cheap reject before spawning python3, since every Bash call lands here.
    [[ $payload == *push* ]] || exit 0

    # `git` at command position, then `push` as its own word, with git's global
    # options allowed in between so `git -C <dir> push` counts. A path such as
    # scripts/git-push.sh does not: its "git" follows "/" and its "push" a "-",
    # so neither sits at a word boundary.
    printf '%s' "$payload" | python3 -c 'import json,sys
try:
    print((json.load(sys.stdin).get("tool_input") or {}).get("command") or "")
except Exception:
    pass' 2>/dev/null \
      | grep -Eq '(^|[[:space:];&|(])git([[:space:]][^;&|]*)?[[:space:]]push([[:space:];&|)]|$)' \
      || exit 0

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
