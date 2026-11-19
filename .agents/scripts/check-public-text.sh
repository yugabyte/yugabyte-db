#!/usr/bin/env bash
# check-public-text: scan text bound for a public artifact (PR title, PR body,
#                    commit messages, branch name) for wording the disclosure
#                    tier of the change does not allow.
#
# The tiers are defined in src/AGENTS.md, "Security and customer-sensitive
# changes". This is a backstop for the scrub the author still has to do: it
# catches the common slips, not everything.
#
# usage: check-public-text.sh -s <tier> [-b <base-ref>] [-t <title>] [file...]
#
#   -s tier   public-cve | embargoed | customer
#   -b ref    Also scan the commit messages in <ref>..HEAD and the branch name.
#   -t title  A title string to scan.
#   file...   Files to scan (description, test plan, upgrade notes).
#
# Env:
#   SENSITIVE_WORDS  Comma-separated strings that must not appear, such as a
#                    customer name. Matched literally, ignoring case.
#
# Exit codes:
#   0  nothing found
#   1  bad arguments
#   7  disallowed wording found (each hit is printed with its source)

set -euo pipefail

tier=""
base_ref=""
title=""

usage() {
  sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2
  exit 1
}

while getopts ":s:b:t:h" opt; do
  case "$opt" in
    s) tier="$OPTARG" ;;
    b) base_ref="$OPTARG" ;;
    t) title="$OPTARG" ;;
    h) usage ;;
    \?) echo "error: unknown option -$OPTARG" >&2; usage ;;
    :)  echo "error: -$OPTARG requires an argument" >&2; usage ;;
  esac
done
shift $((OPTIND - 1))

# No tier may say what is still exposed, how to exploit it, or link an internal system.
all_tiers='unpatched|not (yet )?patched|(still|remains?) (vulnerable|affected|exposed)'
all_tiers+='|exploit|rapidfort\.com|atlassian\.net|slack\.com/archives'
# An embargoed fix must not be recognizable as a security fix at all.
embargoed='CVE-[0-9]{4}-[0-9]+|GHSA(-[0-9a-z]{4}){3}|GO-[0-9]{4}-[0-9]+|vulnerab|security'

case "$tier" in
  public-cve|customer) pattern="$all_tiers" ;;
  embargoed) pattern="${all_tiers}|${embargoed}" ;;
  *) echo "error: -s must be public-cve, embargoed or customer" >&2; usage ;;
esac

found=0

# scan <label> reads text on stdin and prints every disallowed line.
scan() {
  local label="$1" text hits word
  text="$(cat)"
  hits="$(grep -n -i -E -- "$pattern" <<<"$text" || true)"
  if [[ -n "${SENSITIVE_WORDS:-}" ]]; then
    while IFS= read -r word; do
      [[ -z "$word" ]] && continue
      hits+="${hits:+$'\n'}$(grep -n -i -F -- "$word" <<<"$text" || true)"
    done < <(tr ',' '\n' <<<"$SENSITIVE_WORDS")
  fi
  hits="$(grep -v '^$' <<<"$hits" | sort -u -t: -k1,1n || true)"
  if [[ -n "$hits" ]]; then
    found=1
    while IFS= read -r line; do
      echo "  ${label}:${line}" >&2
    done <<<"$hits"
  fi
}

[[ -n "$title" ]] && scan "title" <<<"$title"

for f in "$@"; do
  [[ -f "$f" ]] || { echo "error: file not found: $f" >&2; exit 1; }
  scan "$f" <"$f"
done

if [[ -n "$base_ref" ]]; then
  scan "commit messages" < <(git log --format='%B' "${base_ref}..HEAD")
  scan "branch name" < <(git branch --show-current)
fi

if (( found )); then
  echo "error: the text above is not allowed in a public artifact for a" \
       "'${tier}' change. Move it to the JIRA ticket. See src/AGENTS.md," \
       "\"Security and customer-sensitive changes\"." >&2
  exit 7
fi
