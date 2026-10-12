#!/usr/bin/env bash
# block-git-push: refuse a raw `git push` and name the helper instead.
#
# One script, three registrations. Neither harness filters precisely enough to
# be trusted, so the script reads the command off stdin and decides for itself:
#
#   PreToolUse            Claude. `matcher` sees the tool name only, so this
#                         runs on every Bash call.
#   beforeShellExecution  Cursor, from .cursor/hooks.json. This one is also the
#                         enforcement on that side -- Cursor has no equivalent
#                         of `permissions.deny` -- so its matcher deliberately
#                         over-includes (any word "push") and the check below
#                         narrows. A tighter matcher there would mean a push
#                         spelling it missed ran unblocked.
#   preToolUse            Cursor's *import* of the Claude hook above. Cursor
#                         reads .claude/settings.json, maps PreToolUse/Bash to
#                         preToolUse/Shell, and drops the `if` condition, so
#                         this also runs on every Shell call. Its dedupe is
#                         keyed on the exact command string per event name, so
#                         the beforeShellExecution entry does not cancel it.
#
# hook_event_name therefore picks both the reply dialect and what "not a push"
# has to look like. Silence means "no opinion" to Claude and to the imported
# hook, but Cursor's own entry is failClosed, where no output blocks the
# command -- so that one gets an explicit neutral answer instead.
#
# Enforcement never rests here: `permissions.deny` refuses the push on the
# Claude side and failClosed covers a crash on the Cursor side, so a miss in
# this script costs the message, never the block.

msg='Blocked: raw git push is disabled. Use .agents/scripts/git-push.sh instead. '
msg+='It pushes to your fork, or to upstream when the branch is a '
msg+='feature-stack/<feature>/<change> stack branch.'

payload=$(cat)

# Both harnesses emit compact JSON, so match the key inline and keep the common
# case free of a python3 spawn. An unrecognised payload falls through to the
# neutral answers below.
case $payload in
  *'"hook_event_name":"beforeShellExecution"'*) event=beforeShellExecution ;;
  *'"hook_event_name":"preToolUse"'*)           event=preToolUse ;;
  *'"hook_event_name":"PreToolUse"'*)           event=PreToolUse ;;
  *)                                            event= ;;
esac

# "No opinion." Only Cursor's failClosed entry needs this spelled out; `ask`
# leaves the command to Cursor's normal approval flow rather than asserting it
# is safe, which is the honest answer for a command we did not recognise.
no_opinion() {
  [[ $event == beforeShellExecution ]] && printf '{"permission":"ask"}\n'
  exit 0
}

deny() {
  case $event in
    PreToolUse)
      printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' \
        "$msg"
      ;;
    *)
      printf '{"permission":"deny","user_message":"%s","agent_message":"%s"}\n' "$msg" "$msg"
      ;;
  esac
  exit 0
}

# Cheap reject before spawning python3, since every Bash/Shell call lands here.
[[ $payload == *push* ]] || no_opinion

# Claude nests the command under tool_input; Cursor's beforeShellExecution puts
# it at the top level.
command=$(printf '%s' "$payload" | python3 -c 'import json,sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
t = d.get("tool_input") or {}
print(t.get("command") or d.get("command") or "")' 2>/dev/null)

# `git` at command position, then `push` as its own word. Only git's global
# options may sit in between, each optionally followed by one value, so
# `git -C <dir> push` and `git -c k=v push` count. Keying on the leading "-"
# is what separates those from a different subcommand: `git log --author push`
# and `git commit -m "fix push"` both put a bare word right after "git", so
# neither is a push. A path such as scripts/git-push.sh is out too -- its
# "git" follows "/" and its "push" a "-", so neither sits at a word boundary.
printf '%s' "$command" \
  | grep -Eq '(^|[[:space:];&|(])git([[:space:]]+-[^[:space:];&|]*([[:space:]]+[^-[:space:];&|][^[:space:];&|]*)?)*[[:space:]]+push([[:space:];&|)]|$)' \
  || no_opinion

deny
