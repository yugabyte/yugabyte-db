#!/usr/bin/env bash
# git-push: run the linter, verify the push target, and push HEAD to the
#           user's fork (or, for a stack branch, to upstream).
#
# Designed as the "publish to GitHub" step for create-pr.sh. Run standalone
# to push any branch you have queued up.
#
# Two things decide what this script does:
#
#   Where it pushes.  Normally the user's fork; upstream is refused. The one
#   exception is a `feature-stack/<feature>/<change>` branch, which GitHub's
#   rulesets carve out of "Block Creations" / "Require PR" / "yb_required"
#   precisely so stacked PRs can live in the main repo (a PR stack cannot be
#   assembled out of fork branches). Nothing else may target upstream.
#
#   Whether it may rewrite history.  This script never rebases; picking up a
#   newer base is the user's call. Once a PR is open, rewriting its history
#   (rebase, amend, reset) is not allowed at all: it renews the SHAs under
#   reviewers' comments and loses their diff-since-last-look. So with an open
#   PR the push must be a fast-forward and is never forced. Without one, a
#   rewritten branch is force-pushed with a lease.
#
#   A stack branch is the exception here too. GitHub merges a stack only when
#   its history is linear, and keeps it linear by cascading rebases, so every
#   layer above a change is rewritten whatever its review state. This script
#   lints the whole stack and hands the push to `gh stack push`, which
#   force-pushes each layer with a lease. Rebasing and syncing are gh-stack's
#   job; see the gh-stack skill.
#
# usage: git-push [-b <base>] [-r <fork-remote>]
#
# Optional inputs:
#   -b base   Base branch on the upstream repo to lint against
#             (default: master). Ignored for a feature-stack branch, whose
#             base comes from `gh stack`.
#   -r remote Override fork-remote auto-detection. Useful for unusual
#             remote layouts; otherwise leave unset. Ignored for a
#             feature-stack branch, which always pushes to upstream.
#
# Env overrides:
#   GH_REPO          default: yugabyte/yugabyte-db
#   UPSTREAM_REMOTE  override upstream auto-detection
#   FORK_REMOTE      override fork auto-detection (same as -r)
#
# Exit codes:
#   0  pushed successfully (last log line is `>>> pushed ...`)
#   1  pre-flight failure (no remotes, fork == upstream, dirty tree, etc.)
#   3  lint failed -- fix as a NEW commit (do not amend a pushed commit),
#      then re-run
#   4  the branch has an open PR and the push is not a fast-forward -- the
#      message says how to recover without rewriting the PR's history
#   5  stack branch: `gh stack push` failed -- read its message

set -euo pipefail

base_branch="master"
fork_remote_arg=""
GH_REPO="${GH_REPO:-yugabyte/yugabyte-db}"

usage() {
  cat <<EOF >&2
usage: $(basename "$0") [-b <base>] [-r <fork-remote>]

Lint the current branch and push it. Pushes to your fork, except for a
feature-stack/<feature>/<change> branch, which goes to the upstream repo.
Never rebases. With an open PR the push must be a fast-forward; without
one, a rewritten branch is force-pushed with a lease. A stack branch is
linted whole and pushed with \`gh stack push\`.

Options:
  -b base    Upstream base branch to lint against (default: master).
             Ignored for a stack branch.
  -r remote  Override fork-remote auto-detection.

Env overrides: GH_REPO, UPSTREAM_REMOTE, FORK_REMOTE.
EOF
  exit 1
}

while getopts ":b:r:h" opt; do
  case "$opt" in
    b) base_branch="$OPTARG" ;;
    r) fork_remote_arg="$OPTARG" ;;
    h) usage ;;
    \?) echo "error: unknown option -$OPTARG" >&2; usage ;;
    :)  echo "error: -$OPTARG requires an argument" >&2; usage ;;
  esac
done

command -v gh >/dev/null || { echo "error: 'gh' CLI not found in PATH" >&2; exit 1; }

current_branch=$(git symbolic-ref --short HEAD 2>/dev/null) || {
  echo "error: HEAD is detached; check out a feature branch first" >&2
  exit 1
}

# Detect upstream remote (one whose URL contains $GH_REPO).
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

# A stack branch is the only branch allowed to push to upstream. The team
# convention is feature-stack/<feature-name>/<change-name>, which is also the
# shape the rulesets' `feature-stack/**/*` exclude was written for. Warn
# rather than reject: GitHub is the authority on which names that pattern
# actually admits, and refusing a name it would have accepted is the worse
# failure. If the push does bounce off "Block Creations", this warning is
# already on screen to explain why.
is_stack_branch=false
if [[ "$current_branch" == feature-stack/* ]]; then
  is_stack_branch=true
  if [[ ! "$current_branch" =~ ^feature-stack/[^/]+/[^/]+$ ]]; then
    echo "warn: '$current_branch' does not follow the stack branch" >&2
    echo "      convention feature-stack/<feature-name>/<change-name>." >&2
    echo "      Pushing anyway. If the upstream rulesets reject it, rename:" >&2
    echo "        git branch -m feature-stack/<feature-name>/<change-name>" >&2
  fi
fi

# A stack branch pushes to upstream, so it skips fork detection entirely --
# the user may not even have a fork remote configured.
if ! $is_stack_branch; then
  # Resolve the fork remote: -r > $FORK_REMOTE > auto-detect.
  FORK_REMOTE="${fork_remote_arg:-${FORK_REMOTE:-}}"
  if [[ -z "$FORK_REMOTE" ]]; then
    gh_user=$(gh api user --jq '.login' 2>/dev/null || true)
    repo_name="${GH_REPO#*/}"
    while read -r remote; do
      [[ "$remote" == "$UPSTREAM_REMOTE" ]] && continue
      url=$(git remote get-url "$remote" 2>/dev/null || true)
      if [[ -n "$gh_user" && "$url" == *"${gh_user}/${repo_name}"* ]]; then
        FORK_REMOTE="$remote"
        break
      fi
    done < <(git remote)
  fi
  [[ -z "$FORK_REMOTE" ]] && {
    echo "error: no fork remote found; expected one pointing at" >&2
    echo "       <your-gh-user>/${GH_REPO#*/}. Pass -r <remote> or set FORK_REMOTE." >&2
    exit 1
  }

  # Refuse to push to upstream owner. The fork-detection above checks the URL
  # pattern; this guard catches misconfigured FORK_REMOTE / -r overrides and
  # the edge case where the gh user happens to match the upstream owner.
  fork_url=$(git remote get-url "$FORK_REMOTE")
  # Resolve the owner via `gh repo view` (which accepts the URL form and
  # follows GitHub's canonical owner). Fall back to URL parsing if gh
  # can't reach the API or the URL isn't a github.com one.
  fork_owner=$(gh repo view "$fork_url" --json owner --jq '.owner.login' 2>/dev/null \
               || echo "$fork_url" | sed -E 's|.*[:/]([^:/]+)/[^/]+(\.git)?$|\1|')
  upstream_owner="${GH_REPO%%/*}"
  if [[ "$fork_owner" == "$upstream_owner" ]]; then
    echo "error: refusing to push -- fork remote '$FORK_REMOTE' resolves to" >&2
    echo "       owner '$fork_owner', which matches the upstream repo ($GH_REPO)." >&2
    echo "       This would push to the upstream, not your fork." >&2
    echo "       Only feature-stack/<feature>/<change> branches may target" >&2
    echo "       upstream, and this branch is not one." >&2
    exit 1
  fi
  push_remote="$FORK_REMOTE"
  push_owner="$fork_owner"
  push_target_desc="${fork_owner}/${GH_REPO#*/}"
fi

# Reject tracked-file dirtiness; untracked files are fine.
if [[ -n "$(git status --porcelain | grep -v '^??' || true)" ]]; then
  echo "error: working tree has uncommitted tracked changes; commit first" >&2
  git status --short >&2
  exit 1
fi

# Ensure the linter is happy. Never push if lint isn't clean.
# Resolve the repo root so `build-support/lint.sh` works regardless of
# the caller's cwd (a subdirectory invocation otherwise hits "no such file").
repo_root=$(git rev-parse --show-toplevel)
run_lint() {
  echo ">>> running ${repo_root}/build-support/lint.sh --rev $1"
  if ! "${repo_root}/build-support/lint.sh" --rev "$1"; then
    echo "" >&2
    echo "error: lint failed. Fix issues as a NEW commit" >&2
    echo "       (do not amend a pushed commit), then re-run this script." >&2
    exit 3
  fi
}

if $is_stack_branch; then
  if ! gh stack --help >/dev/null 2>&1; then
    echo "error: stack branches are pushed with the gh-stack extension, which" >&2
    echo "       is not installed. See the gh-stack skill." >&2
    exit 1
  fi
  if ! stack_json=$(gh stack view --json); then
    echo "error: ${current_branch} is not in a local gh stack. Track the" >&2
    echo "       existing layers, bottom first, with" >&2
    echo "         gh stack init <bottom-branch> ... ${current_branch}" >&2
    exit 1
  fi
  read -r trunk top_branch < <(python3 -c 'import json, sys
s = json.load(sys.stdin)
live = [b["name"] for b in s["branches"] if not b.get("isMerged")]
print(s["trunk"], live[-1] if live else "")' <<< "$stack_json")

  echo ">>> fetching ${UPSTREAM_REMOTE}/${trunk}"
  git fetch "$UPSTREAM_REMOTE" \
    "+refs/heads/${trunk}:refs/remotes/${UPSTREAM_REMOTE}/${trunk}"

  # `gh stack push` sends every layer, and only the top layer's tree holds
  # all of them, so lint there.
  if [[ -n "$top_branch" && "$top_branch" != "$current_branch" ]]; then
    if ! git checkout --quiet "$top_branch"; then
      echo "error: could not check out the top layer '${top_branch}' to lint" >&2
      echo "       the stack. If another worktree has it, run this there." >&2
      exit 1
    fi
    trap 'git checkout --quiet "$current_branch"' EXIT
  fi
  run_lint "${UPSTREAM_REMOTE}/${trunk}"

  echo ">>> gh stack push --remote ${UPSTREAM_REMOTE} (${GH_REPO})"
  gh stack push --remote "$UPSTREAM_REMOTE" || exit 5
  echo ">>> pushed stack ${trunk} <- ... <- ${top_branch} to ${GH_REPO}"
  exit 0
fi

# Look the PR up before pushing: an open PR forbids rewriting the branch, and
# its number/title/url drive the summary-sync reminder at the end. `--head`
# matches the branch name across every fork, so keep only PRs whose head is
# in the fork. A failed lookup aborts: guessing "no PR" would force-push over
# a branch that may be under review.
pr_num=""
pr_title=""
pr_url=""
if ! pr_info=$(gh pr list -R "$GH_REPO" --head "$current_branch" \
                 --state open --json number,url,title,headRepositoryOwner \
                 --jq "[.[] | select(.headRepositoryOwner.login == \"${push_owner}\")][0]
                       | select(. != null)
                       | \"\(.number)\t\(.title)\t\(.url)\""); then
  echo "error: could not look up the PR for ${current_branch} on ${GH_REPO}," >&2
  echo "       so whether the branch may be force-pushed is unknown." >&2
  echo "       Check 'gh auth status' and network, then re-run." >&2
  exit 1
fi
if [[ -n "$pr_info" ]]; then
  IFS=$'\t' read -r pr_num pr_title pr_url <<< "$pr_info"
fi

# Don't fetch the fork branch, so --force-with-lease below checks against the
# last state the user saw and rejects the push if commits landed there since
# (e.g. from another machine).
remote_branch_exists=false
if git rev-parse --verify --quiet "refs/remotes/${push_remote}/${current_branch}" \
     >/dev/null 2>&1; then
  remote_branch_exists=true
fi

# With an open PR, HEAD must still contain what was last pushed.
if [[ -n "$pr_num" ]] && $remote_branch_exists; then
  pushed="${push_remote}/${current_branch}"
  if ! git merge-base --is-ancestor "$pushed" HEAD; then
    echo "" >&2
    echo "error: ${current_branch} does not contain ${pushed}, and PR #${pr_num}" >&2
    echo "       is open. Rewriting an open PR's history is not allowed." >&2
    if git merge-base --is-ancestor HEAD "$pushed"; then
      echo "       ${pushed} is ahead of you. Integrate it:" >&2
      echo "         git merge --ff-only ${pushed}" >&2
    else
      echo "       If ${pushed} has commits you fetched but did not merge:" >&2
      echo "         git merge ${pushed}" >&2
      echo "       If a rebase, amend, or reset rewrote the branch (HEAD was" >&2
      echo "       $(git rev-parse --short HEAD); see git reflog), redo the" \
           "change as new commits:" >&2
      echo "         amend or squash:  git reset --soft ${pushed}, then commit" >&2
      echo "         rebase onto base: git reset --hard ${pushed}, then" >&2
      echo "                           git merge ${UPSTREAM_REMOTE}/${base_branch}" >&2
    fi
    exit 4
  fi
fi

# Explicit destination: a plain `git fetch <remote> <branch>` only updates
# the remote-tracking ref when the configured refspec covers that branch, and
# everything below reads the tracking ref.
echo ">>> fetching ${UPSTREAM_REMOTE}/${base_branch}"
git fetch "$UPSTREAM_REMOTE" \
  "+refs/heads/${base_branch}:refs/remotes/${UPSTREAM_REMOTE}/${base_branch}"

behind=$(git rev-list --count "HEAD..${UPSTREAM_REMOTE}/${base_branch}")
if (( behind > 0 )); then
  echo ">>> note: ${current_branch} is ${behind} commit(s) behind" \
       "${UPSTREAM_REMOTE}/${base_branch}, pushing it as is"
  if [[ -n "$pr_num" ]]; then
    echo "    To pick it up without rewriting PR #${pr_num}:" \
         "git merge ${UPSTREAM_REMOTE}/${base_branch}"
  fi
fi

run_lint "${UPSTREAM_REMOTE}/${base_branch}"

# Capture the pre-push remote SHA (empty on first push) so we can list the
# new commits afterwards and remind the user/agent to keep the PR summary in sync.
pre_push_sha=$(git rev-parse --verify --quiet \
                 "${push_remote}/${current_branch}" 2>/dev/null || true)

if [[ -n "$pr_num" ]]; then
  echo ">>> pushing ${current_branch} -> ${push_remote} (${push_target_desc})," \
       "fast-forward only: PR #${pr_num} is open"
  if ! git push -u "$push_remote" HEAD; then
    echo "" >&2
    echo "error: push rejected. If ${push_remote}/${current_branch} has commits" >&2
    echo "       you have not fetched, integrate them without rewriting:" >&2
    echo "         git fetch ${push_remote} ${current_branch}" >&2
    echo "         git merge ${push_remote}/${current_branch}" >&2
    exit 4
  fi
elif $remote_branch_exists; then
  echo ">>> force-pushing ${current_branch} -> ${push_remote}" \
       "(${push_target_desc}) --force-with-lease"
  git push --force-with-lease -u "$push_remote" HEAD
else
  echo ">>> pushing ${current_branch} -> ${push_remote} (${push_target_desc})"
  git push -u "$push_remote" HEAD
fi
echo ">>> pushed ${push_target_desc}:${current_branch}"

# If this push lands on an existing open PR, surface the new commits and
# remind the caller to evaluate whether the PR summary still describes the
# branch. The summary stays in sync only if a human (or AI) makes the call --
# so we print the data, we don't enforce.
if [[ -n "$pre_push_sha" && -n "$pr_num" ]]; then
  new_subjects=$(git log --format='  - %s' "${pre_push_sha}..HEAD" 2>/dev/null || true)
  if [[ -n "$new_subjects" ]]; then
    echo ""
    echo ">>> PR #${pr_num} updated: ${pr_url}"
    echo ">>> current title: ${pr_title}"
    echo ">>> new commits in this push:"
    echo "$new_subjects"

    echo ""
    echo ">>> Review the PR title and summary. Update either if these commits"
    echo "    significantly shift scope, approach, or component. Leave them"
    echo "    alone for refinements within existing scope, lint fixes, typos,"
    echo "    comment-only edits, or pure-refactor commits."
    echo "    Title update (rare; only when the existing title misleads):"
    echo "      gh api -X PATCH /repos/${GH_REPO}/pulls/${pr_num} -f title='<new>'"
    echo "    Body update:"
    echo "      jq -Rs '{body: .}' < /tmp/claude/pr-body-${pr_num}.md \\"
    echo "        | gh api -X PATCH /repos/${GH_REPO}/pulls/${pr_num} --input -"
  fi
fi
