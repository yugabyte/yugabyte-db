#!/usr/bin/env bash
# git-push: run the linter, verify the push target is the user's fork
#            (not the upstream repo), and push HEAD to that fork.
#
# Designed as the common "publish to GitHub" step for create-pr.sh and
# backport-commit.sh -- both have to lint and both have to refuse to push
# to upstream. Run standalone to push any branch you have queued up.
#
# usage: git-push [-b <base>] [-r <fork-remote>]
#
# Optional inputs:
#   -b base   Base branch on the upstream repo to lint against
#             (default: master). Lint runs as
#             `./build-support/lint.sh --rev <upstream-remote>/<base>`.
#   -r remote Override fork-remote auto-detection. Useful for unusual
#             remote layouts; otherwise leave unset.
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

set -euo pipefail

base_branch="master"
fork_remote_arg=""
GH_REPO="${GH_REPO:-yugabyte/yugabyte-db}"

usage() {
  cat <<EOF >&2
usage: $(basename "$0") [-b <base>] [-r <fork-remote>]

Lint the current branch, verify the push target is a fork (not upstream),
and push HEAD to that fork.

Options:
  -b base    Upstream base branch to lint against (default: master).
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

current_branch=$(git symbolic-ref --short HEAD 2>/dev/null) || {
  echo "error: HEAD is detached; check out a feature branch first" >&2
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
  exit 1
fi

# Reject tracked-file dirtiness; untracked files are fine.
if [[ -n "$(git status --porcelain | grep -v '^??' || true)" ]]; then
  echo "error: working tree has uncommitted tracked changes; commit first" >&2
  git status --short >&2
  exit 1
fi

# Always integrate any commits already on the fork branch (e.g. pushed
# from another machine or another agent), then rebase onto the latest
# upstream/<base>. Force-with-lease at push time is the planned outcome
# -- this script rewrites SHAs every run by design so each push lands on
# top of fresh master. Reviewers' line comments may show as "outdated"
# after a rebase that touches their lines; that's the accepted tradeoff
# for keeping the branch current.
remote_branch_exists=false
if git rev-parse --verify --quiet "refs/remotes/${FORK_REMOTE}/${current_branch}" \
     >/dev/null 2>&1; then
  remote_branch_exists=true
  echo ">>> fetching ${FORK_REMOTE}/${current_branch}"
  git fetch "$FORK_REMOTE" "$current_branch"
  echo ">>> rebasing onto ${FORK_REMOTE}/${current_branch}"
  if ! git rebase "${FORK_REMOTE}/${current_branch}"; then
    echo "" >&2
    echo "error: rebase onto ${FORK_REMOTE}/${current_branch} failed." >&2
    echo "       Resolve the conflicts, 'git add' the resolved files," >&2
    echo "       run 'git rebase --continue', then re-run this script." >&2
    exit 2
  fi
fi

echo ">>> fetching ${UPSTREAM_REMOTE}/${base_branch}"
git fetch "$UPSTREAM_REMOTE" "$base_branch"

echo ">>> rebasing onto ${UPSTREAM_REMOTE}/${base_branch}"
if ! git rebase "${UPSTREAM_REMOTE}/${base_branch}"; then
  echo "" >&2
  echo "error: rebase onto ${UPSTREAM_REMOTE}/${base_branch} failed." >&2
  echo "       Resolve the conflicts, 'git add' the resolved files," >&2
  echo "       run 'git rebase --continue', then re-run this script." >&2
  exit 2
fi

# Ensure the linter is happy. Never push if lint isn't clean.
# Resolve the repo root so `build-support/lint.sh` works regardless of
# the caller's cwd (a subdirectory invocation otherwise hits "no such file").
repo_root=$(git rev-parse --show-toplevel)
echo ">>> running ${repo_root}/build-support/lint.sh --rev ${UPSTREAM_REMOTE}/${base_branch}"
if ! "${repo_root}/build-support/lint.sh" --rev "${UPSTREAM_REMOTE}/${base_branch}"; then
  echo "" >&2
  echo "error: lint failed. Fix issues as a NEW commit" >&2
  echo "       (do not amend a pushed commit), then re-run this script." >&2
  exit 3
fi

# Capture the pre-push remote SHA (empty on first push) so we can list the
# new commits afterwards and remind the user/agent to keep the PR summary in sync.
pre_push_sha=$(git rev-parse --verify --quiet \
                 "${FORK_REMOTE}/${current_branch}" 2>/dev/null || true)

if $remote_branch_exists; then
  echo ">>> force-pushing ${current_branch} -> ${FORK_REMOTE}" \
       "(${fork_owner}/${GH_REPO#*/}) --force-with-lease"
  git push --force-with-lease -u "$FORK_REMOTE" HEAD
else
  echo ">>> pushing ${current_branch} -> ${FORK_REMOTE} (${fork_owner}/${GH_REPO#*/})"
  git push -u "$FORK_REMOTE" HEAD
fi
echo ">>> pushed ${fork_owner}:${current_branch}"

# If this push lands on an existing open PR, surface the new commits and
# remind the caller to evaluate whether the PR summary still describes the
# branch. The summary stays in sync only if a human (or AI) makes the call --
# so we print the data, we don't enforce.
if [[ -n "$pre_push_sha" ]] && command -v gh >/dev/null 2>&1; then
  # gh pr list --head takes a bare branch name, not <owner>:<branch>;
  # gh matches the head branch within the target repo's PRs.
  # Tab-separated so titles with spaces survive `read`.
  pr_info=$(gh pr list -R "$GH_REPO" --head "$current_branch" \
              --state open --json number,url,title \
              --jq '.[0] | select(. != null) | "\(.number)\t\(.title)\t\(.url)"' \
              2>/dev/null || true)
  if [[ -n "$pr_info" ]]; then
    IFS=$'\t' read -r pr_num pr_title pr_url <<< "$pr_info"
    new_subjects=$(git log --format='  - %s' "${pre_push_sha}..HEAD" 2>/dev/null || true)
    if [[ -n "$new_subjects" ]]; then
      echo ""
      echo ">>> PR #${pr_num} updated: ${pr_url}"
      echo ">>> current title: ${pr_title}"
      echo ">>> new commits in this push:"
      echo "$new_subjects"

      # Re-trigger Gemini Code Assist on the new commits.
      if gh pr comment "$pr_num" -R "$GH_REPO" --body "/gemini review" \
            >/dev/null 2>&1; then
        echo ">>> requested Gemini review (/gemini review)"
      else
        echo "warn: failed to post '/gemini review' on PR #${pr_num}" >&2
      fi

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
fi
