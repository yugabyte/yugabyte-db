#!/usr/bin/env bash
# new-pr: rebase the current branch on upstream/<base>, run the linter, push
#         to the user's fork, and open a cross-repo PR with reviewers.
#
# Designed to be invoked from the /create-pr Claude Code skill once metadata
# (issue, title, body, reviewers) has been gathered. Usable standalone too.
#
# usage: new-pr -i <issue> -t <title> -d <description-file> -T <test-plan-file>
#               [-U <upgrade-file>] [-r <reviewers>] [-b <base>] [-D]
#
# Required inputs:
#   -i issue       GitHub issue number ("31151" or "#31151") or JIRA key
#                  ("PLAT-20518"). Prepended to the title as "[<issue>] ...".
#   -t title       Title body including the "Component: " prefix
#                  (e.g. "DocDB: Fix flake in SamplingProfilerTest").
#                  The "[<issue>] " prefix is added automatically.
#   -d file        Path to a markdown file with the PR description (the
#                  "what / why" prose). Becomes the "## Summary" section of
#                  the body.
#   -T file        Path to a markdown file with the test plan (typically a
#                  checkbox list of tests run / to run). Becomes the
#                  "## Test plan" section of the body. Required -- callers
#                  must always supply one, even for trivial PRs.
#
# Optional:
#   -U file        Path to a markdown file with upgrade/rollback notes.
#                  Becomes the "## Upgrade/Rollback safety" section between
#                  Summary and Test plan. **Required when the branch
#                  changes any *.proto file** -- the script aborts otherwise.
#   -r reviewers   Comma-separated GH user logins and/or team slugs
#                  (e.g. "alice,bob,yugabyte/db-approvers"). Team slugs
#                  arrive as "org/slug"; the org/ prefix is stripped before
#                  routing the slug to team_reviewers[].
#   -b base        Base branch on the upstream repo (default: master).
#   -D             Open the PR as a GitHub draft. Useful when you want to
#                  read the rendered PR before notifications fire and before
#                  reviewers are auto-pinged. Convert with
#                  `gh pr ready <num>` when ready for review.
#
# Env overrides:
#   GH_REPO         default: yugabyte/yugabyte-db
#   UPSTREAM_REMOTE auto-detected as the remote pointing at $GH_REPO
#   FORK_REMOTE     auto-detected as the non-upstream remote pointing at
#                   <gh-auth-user>/<repo-name>
#
# Exit codes:
#   0  success (PR URL printed last)
#   1  pre-flight failure (bad args, dirty tree, missing remote, etc.)
#   2  rebase conflict -- resolve, `git rebase --continue`, then re-run
#   3  lint failed -- fix as a NEW commit (do not amend), then re-run

set -euo pipefail

issue=""
title=""
description_file=""
test_plan_file=""
upgrade_file=""
reviewers=""
base_branch="master"
draft=0
GH_REPO="${GH_REPO:-yugabyte/yugabyte-db}"

usage() {
  cat <<EOF >&2
usage: $(basename "$0") -i <issue> -t <title> -d <description-file> -T <test-plan-file> \\
                        [-U <upgrade-file>] [-r <reviewers>] [-b <base>] [-D]

Required:
  -i issue       GitHub issue (#NNNN, NNNN) or JIRA key (PLAT-NNN)
  -t title       Title with "Component: " prefix (e.g. "DocDB: Fix flake")
  -d file        Path to PR description (markdown). Becomes "## Summary".
  -T file        Path to test plan (markdown). Becomes "## Test plan".

Optional:
  -U file        Path to upgrade/rollback notes (markdown). Becomes
                 "## Upgrade/Rollback safety", inserted between Summary
                 and Test plan. **Required when the branch changes any
                 .proto file** -- the script aborts otherwise.
  -r reviewers   Comma-separated handles and/or team slugs (org/slug)
  -b base        Base branch (default: master)
  -D             Open the PR as a GitHub draft (\`gh pr create --draft\`).
                 Convert with \`gh pr ready <num>\` when ready for review.

Example:
  $(basename "$0") -i 31151 -t "DocDB: Fix flake in SamplingProfilerTest" \\
                   -d /tmp/desc.md -T /tmp/testplan.md \\
                   -r alice,bob,yugabyte/db-approvers
EOF
  exit 1
}

while getopts ":i:t:d:T:U:r:b:Dh" opt; do
  case "$opt" in
    i) issue="$OPTARG" ;;
    t) title="$OPTARG" ;;
    d) description_file="$OPTARG" ;;
    T) test_plan_file="$OPTARG" ;;
    U) upgrade_file="$OPTARG" ;;
    r) reviewers="$OPTARG" ;;
    b) base_branch="$OPTARG" ;;
    D) draft=1 ;;
    h) usage ;;
    \?) echo "error: unknown option -$OPTARG" >&2; usage ;;
    :)  echo "error: -$OPTARG requires an argument" >&2; usage ;;
  esac
done

[[ -z "$issue" || -z "$title" || -z "$description_file" || -z "$test_plan_file" ]] && usage

# Normalize the reviewer list: accept space- or comma-separated, collapse
# repeated commas, strip leading/trailing commas. Without this, an input
# like "alice, bob" splits into "alice" and " bob" (with a leading space),
# which the requested_reviewers REST endpoint rejects.
if [[ -n "$reviewers" ]]; then
  reviewers=$(echo "$reviewers" | tr ' ' ',' | tr -s ',' | sed -e 's/^,//' -e 's/,$//')
fi
[[ ! -f "$description_file" ]] && {
  echo "error: description file not found: $description_file" >&2; exit 1;
}
[[ ! -f "$test_plan_file" ]] && {
  echo "error: test plan file not found: $test_plan_file" >&2; exit 1;
}
[[ ! -s "$description_file" ]] && {
  echo "error: description file is empty: $description_file" >&2; exit 1;
}
[[ ! -s "$test_plan_file" ]] && {
  echo "error: test plan file is empty: $test_plan_file" >&2; exit 1;
}
if [[ -n "$upgrade_file" ]]; then
  [[ ! -f "$upgrade_file" ]] && {
    echo "error: upgrade file not found: $upgrade_file" >&2; exit 1;
  }
  [[ ! -s "$upgrade_file" ]] && {
    echo "error: upgrade file is empty: $upgrade_file" >&2; exit 1;
  }
fi
command -v gh >/dev/null || { echo "error: 'gh' CLI not found in PATH" >&2; exit 1; }

# Normalize -i: accept a single issue or a comma-separated list. Bare digits
# get a "#" prefix; JIRA keys are left alone. Each token is validated; the
# rebuilt list joins with ", " for the canonical "[#a, #b, PLAT-c] ..." prefix.
normalized_issue=""
sep=""
IFS=',' read -ra _issue_tokens <<< "$issue"
for _tok in "${_issue_tokens[@]}"; do
  # Trim leading/trailing whitespace.
  _tok="${_tok#"${_tok%%[![:space:]]*}"}"
  _tok="${_tok%"${_tok##*[![:space:]]}"}"
  [[ -z "$_tok" ]] && continue
  if [[ "$_tok" =~ ^[0-9]+$ ]]; then
    _tok="#${_tok}"
  fi
  if ! [[ "$_tok" =~ ^(#[0-9]+|[A-Z][A-Z0-9]*-[0-9]+)$ ]]; then
    echo "error: -i tokens must be GH issues (#NNNN, NNNN) or JIRA keys" >&2
    echo "       (PROJECT-NNN); use a comma-separated list for multiple." >&2
    echo "       got: $_tok" >&2
    exit 1
  fi
  normalized_issue="${normalized_issue}${sep}${_tok}"
  sep=", "
done
[[ -z "$normalized_issue" ]] && {
  echo "error: -i is empty after normalization" >&2; exit 1;
}
issue="$normalized_issue"

# If the user accidentally included an issue prefix in the title, strip it
# along with any whitespace that follows. We add the canonical
# "[<issue>] " prefix below; without the trim, an input like
# "[#123]  Component: Title" (with two spaces) would leave a leading
# space and trip the validation regex.
title="${title#\[*\]}"
title="${title#"${title%%[![:space:]]*}"}"

# Validate the title body is "<Component>: <Title>". Component must start with
# a letter and contain at least one alphanumeric char; followed by ": " and
# something. This catches typos, missing prefixes, and unrelated formats.
if ! [[ "$title" =~ ^[A-Za-z][A-Za-z0-9]+:[[:space:]].+ ]]; then
  echo "error: -t must be '<Component>: <Title>' (e.g. 'DocDB: Fix flake')" >&2
  echo "       got: $title" >&2
  echo "       common components: DocDB, YSQL, YCQL, YBA, CDC, xCluster," >&2
  echo "       yugabyted, Docs, Build" >&2
  exit 1
fi

full_title="[${issue}] ${title}"

# Detect upstream remote (one that points at $GH_REPO).
UPSTREAM_REMOTE="${UPSTREAM_REMOTE:-}"
if [[ -z "$UPSTREAM_REMOTE" ]]; then
  while read -r remote; do
    url=$(git remote get-url "$remote" 2>/dev/null || true)
    if [[ "$url" == *"$GH_REPO"* ]]; then
      UPSTREAM_REMOTE="$remote"
      break
    fi
  done < <(git remote)
fi
[[ -z "$UPSTREAM_REMOTE" ]] && {
  echo "error: no remote points at $GH_REPO; add one with:" >&2
  echo "       git remote add upstream git@github.com:${GH_REPO}.git" >&2
  exit 1
}

# Resolve the gh-authenticated user; we'll use this both to find the
# fork remote (it must point at <gh_user>/<repo>) and as the fork
# owner for the cross-repo PR head spec, which lets us skip a brittle
# URL-parse-then-extract-owner step further down.
gh_user=$(gh api user --jq '.login' 2>/dev/null || true)
[[ -z "$gh_user" ]] && {
  echo "error: 'gh api user' returned no login; run 'gh auth login' first" >&2
  exit 1
}

# Detect fork remote (a non-upstream remote whose URL contains
# "<gh_user>/<repo_name>").
FORK_REMOTE="${FORK_REMOTE:-}"
if [[ -z "$FORK_REMOTE" ]]; then
  repo_name="${GH_REPO#*/}"
  while read -r remote; do
    [[ "$remote" == "$UPSTREAM_REMOTE" ]] && continue
    url=$(git remote get-url "$remote" 2>/dev/null || true)
    if [[ "$url" == *"${gh_user}/${repo_name}"* ]]; then
      FORK_REMOTE="$remote"
      break
    fi
  done < <(git remote)
fi
[[ -z "$FORK_REMOTE" ]] && {
  echo "error: no fork remote found; expected a remote pointing at" >&2
  echo "       <your-gh-user>/${GH_REPO#*/}. Set FORK_REMOTE=<name> to override." >&2
  exit 1
}

current_branch=$(git symbolic-ref --short HEAD 2>/dev/null) || {
  echo "error: HEAD is detached; check out a feature branch first" >&2
  exit 1
}
[[ "$current_branch" == "$base_branch" ]] && {
  echo "error: refuse to open a PR from $base_branch into itself" >&2
  exit 1
}

echo ">>> upstream=${UPSTREAM_REMOTE}, fork=${FORK_REMOTE}," \
     "base=${base_branch}, branch=${current_branch}"

# Lint, validate destination, and push via the shared helper. git-push.sh
# does its own remote detection but we pass UPSTREAM_REMOTE/FORK_REMOTE
# explicitly via env to keep the two scripts agreeing on which remotes to
# use, and to skip the second auto-detect.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_REMOTE="$UPSTREAM_REMOTE" FORK_REMOTE="$FORK_REMOTE" GH_REPO="$GH_REPO" \
  "${script_dir}/git-push.sh" -b "$base_branch"

# Wire-format / on-disk-format compatibility: any change to a .proto file
# can break upgrade-rollback safety on a mixed-version cluster, so require
# -U with explicit forward/backward/rollback notes. Soft cases (gflag
# default flips, catalog schema bumps) should also pass -U, but we can't
# detect those mechanically -- the proto-file rule is the hard gate. Run
# this *after* git-push.sh so the upstream tracking ref is current; the
# diff above the rebase + push relied on a possibly-stale base.
if [[ -z "$upgrade_file" ]]; then
  proto_changed=$(git diff --name-only \
                    "${UPSTREAM_REMOTE}/${base_branch}" HEAD -- '*.proto' \
                    2>/dev/null || true)
  if [[ -n "$proto_changed" ]]; then
    echo "" >&2
    echo "error: this branch changes .proto files but -U <upgrade-file>" >&2
    echo "       was not passed." >&2
    echo "       Changed protos:" >&2
    echo "$proto_changed" | sed 's/^/         /' >&2
    echo "       Pass -U with a markdown file explaining how the" >&2
    echo "       schema / wire change behaves on a mixed-version cluster" >&2
    echo "       (forward & backward) and what rollback looks like." >&2
    exit 1
  fi
fi

# Fork owner for the gh pr create -H field is the authenticated gh
# user -- the FORK_REMOTE detection above already required the URL to
# match <gh_user>/<repo>, so URL parsing here would just rederive the
# same value (and got fragile for SSH aliases / non-standard remote URLs).
pr_head="${gh_user}:${current_branch}"

if (( draft )); then
  echo ">>> creating PR (draft): ${full_title}"
else
  echo ">>> creating PR: ${full_title}"
fi
# Assemble the body: description -> ## Summary, optional -> ## Upgrade/
# Rollback safety, test plan -> ## Test plan. Forcing each as a separate
# arg means a caller can't accidentally omit a section.
# Use the full-template form `"${TMPDIR:-/tmp}/new-pr-body.XXXXXX"`: the
# `-t` form is BSD/macOS-style and on Linux gnu-mktemp it produces
# `/tmp/new-pr-body.XXXXXX.<random>` (an extra suffix). The full-template
# form respects the X placeholders identically on both platforms.
combined_body=$(mktemp "${TMPDIR:-/tmp}/new-pr-body.XXXXXX")
trap 'rm -f "$combined_body"' EXIT
{
  echo "## Summary"
  echo
  cat "$description_file"
  echo
  if [[ -n "$upgrade_file" ]]; then
    echo "## Upgrade/Rollback safety"
    echo
    cat "$upgrade_file"
    echo
  fi
  echo "## Test plan"
  echo
  cat "$test_plan_file"
} > "$combined_body"
gh_pr_create_args=(-R "$GH_REPO" -B "$base_branch" -H "$pr_head"
                   -t "$full_title" -F "$combined_body")
if (( draft )); then
  gh_pr_create_args+=(--draft)
fi
pr_url=$(gh pr create "${gh_pr_create_args[@]}")
echo "$pr_url"
pr_num="${pr_url##*/}"

if [[ -n "$reviewers" && "$pr_num" =~ ^[0-9]+$ ]]; then
  echo ">>> requesting reviewers: ${reviewers}"
  # User logins go to reviewers[]; team slugs (org/slug) get the org/ prefix
  # stripped and routed to team_reviewers[]. We use the REST endpoint because
  # `gh pr edit --add-reviewer` errors out on this repo with the projectCards
  # GraphQL deprecation.
  IFS=',' read -ra reviewer_array <<< "$reviewers"
  api_args=()
  for r in "${reviewer_array[@]}"; do
    [[ -z "$r" ]] && continue
    if [[ "$r" == */* ]]; then
      api_args+=(-f "team_reviewers[]=${r#*/}")
    else
      api_args+=(-f "reviewers[]=${r}")
    fi
  done
  if (( ${#api_args[@]} > 0 )); then
    if ! gh api -X POST \
           "repos/${GH_REPO}/pulls/${pr_num}/requested_reviewers" \
           "${api_args[@]}" >/dev/null; then
      echo "warn: failed to add reviewers to PR #${pr_num} (continuing): ${reviewers}" >&2
    fi
  fi
fi

echo ">>> done: ${pr_url}"
