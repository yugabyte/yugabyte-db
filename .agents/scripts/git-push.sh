#!/usr/bin/env bash
# git-push: run the linter, pick a push mode from the PR's review state, and
#            push HEAD to the user's fork (or, for a stack branch, upstream).
#
# Designed as the common "publish to GitHub" step for create-pr.sh and
# backport-commit.sh -- both have to lint and both have to keep the push off
# upstream. Run standalone to push any branch you have queued up.
#
# Two things decide what this script does:
#
#   Where it pushes.  Normally the user's fork; upstream is refused. The one
#   exception is a `feature-stack/<feature>/<change>` branch, which GitHub's
#   rulesets carve out of "Block Creations" / "Require PR" / "yb_required"
#   precisely so stacked PRs can live in the main repo (a PR stack cannot be
#   assembled out of fork branches). Nothing else may target upstream.
#
#   Whether it rewrites history.  A branch whose PR is already out of draft is
#   being read: rebasing it renews every SHA, which marks reviewers' line
#   comments "outdated" and loses the diff-since-last-look. So once a PR is
#   ready for review this script appends -- fetch, verify fast-forward, plain
#   push. Before that (no PR yet, or still a draft) nobody is reading, and it
#   rebases onto fresh upstream/<base> and force-pushes as before. `-f` forces
#   the rebase path when the author and reviewer agree it is worth the churn.
#
# usage: git-push [-b <base>] [-r <fork-remote>] [-f]
#
# Optional inputs:
#   -b base   Base branch on the upstream repo to lint against
#             (default: master). For a stacked PR this is the parent
#             `feature-stack/...` branch, not master.
#   -r remote Override fork-remote auto-detection. Useful for unusual
#             remote layouts; otherwise leave unset. Ignored for a
#             feature-stack branch, which always pushes to upstream.
#   -f        Rebase and force-push even when the PR is ready for review.
#
# Env overrides:
#   GH_REPO          default: yugabyte/yugabyte-db
#   UPSTREAM_REMOTE  override upstream auto-detection
#   FORK_REMOTE      override fork auto-detection (same as -r)
#
# Exit codes:
#   0  pushed successfully (last log line is `>>> pushed ...`)
#   1  pre-flight failure (no remotes, fork == upstream, dirty tree, etc.)
#   2  rebase conflict -- resolve, `git rebase --continue`, then re-run
#   3  lint failed -- fix as a NEW commit (do not amend a pushed commit),
#      then re-run
#   4  append-only push is not a fast-forward -- integrate the remote branch
#      with a merge (not a rebase), or re-run with -f to rewrite anyway

set -euo pipefail

base_branch="master"
fork_remote_arg=""
force_rewrite=0
GH_REPO="${GH_REPO:-yugabyte/yugabyte-db}"

usage() {
  cat <<EOF >&2
usage: $(basename "$0") [-b <base>] [-r <fork-remote>] [-f]

Lint the current branch and push it. Pushes to your fork, except for a
feature-stack/<feature>/<change> branch, which goes to the upstream repo.
Rebases and force-pushes until the PR leaves draft; appends after that.

Options:
  -b base    Upstream base branch to lint against (default: master).
             For a stacked PR, the parent feature-stack/... branch.
  -r remote  Override fork-remote auto-detection.
  -f         Rebase and force-push even after the PR is ready for review.

Env overrides: GH_REPO, UPSTREAM_REMOTE, FORK_REMOTE.
EOF
  exit 1
}

while getopts ":b:r:fh" opt; do
  case "$opt" in
    b) base_branch="$OPTARG" ;;
    r) fork_remote_arg="$OPTARG" ;;
    f) force_rewrite=1 ;;
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

if $is_stack_branch; then
  # Stacked PRs are assembled from branches in the main repo, so this is the
  # sanctioned upstream push. Skip fork detection entirely -- the user may
  # not even have a fork remote configured.
  push_remote="$UPSTREAM_REMOTE"
  push_target_desc="$GH_REPO"
  echo ">>> stack branch: pushing to upstream ($GH_REPO)"
else
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
  push_target_desc="${fork_owner}/${GH_REPO#*/}"
fi

# Reject tracked-file dirtiness; untracked files are fine.
if [[ -n "$(git status --porcelain | grep -v '^??' || true)" ]]; then
  echo "error: working tree has uncommitted tracked changes; commit first" >&2
  git status --short >&2
  exit 1
fi

# Look the PR up once: its draft state picks the push mode below, and its
# number/title/url drive the summary-sync reminder at the end.
pr_num=""
pr_title=""
pr_url=""
pr_is_draft=""
pr_info=$(gh pr list -R "$GH_REPO" --head "$current_branch" \
            --state open --json number,url,title,isDraft \
            --jq '.[0] | select(. != null) | "\(.number)\t\(.isDraft)\t\(.title)\t\(.url)"' \
            2>/dev/null || true)
if [[ -n "$pr_info" ]]; then
  IFS=$'\t' read -r pr_num pr_is_draft pr_title pr_url <<< "$pr_info"
fi

# Append-only once the PR is out of draft: reviewers are reading it, and a
# rebase would renew every SHA under their comments. -f opts back out.
append_only=false
if [[ "$pr_is_draft" == "false" ]] && (( ! force_rewrite )); then
  append_only=true
fi

if $append_only; then
  echo ">>> PR #${pr_num} is ready for review -- append-only push" \
       "(no rebase, no force). Pass -f to override."
else
  if [[ -n "$pr_num" ]]; then
    echo ">>> PR #${pr_num} is a draft -- rebase + force-push"
  else
    echo ">>> no open PR for ${current_branch} -- rebase + force-push"
  fi
fi

# Ask the remote rather than trusting a local remote-tracking ref, which a
# fresh clone or a `git fetch` with a narrow refspec may simply not have.
remote_branch_exists=false
if git ls-remote --exit-code --heads "$push_remote" "$current_branch" \
     >/dev/null 2>&1; then
  remote_branch_exists=true
  echo ">>> fetching ${push_remote}/${current_branch}"
  git fetch "$push_remote" "$current_branch"
fi

echo ">>> fetching ${UPSTREAM_REMOTE}/${base_branch}"
git fetch "$UPSTREAM_REMOTE" "$base_branch"

if $append_only; then
  # No rebase. Just prove the push is a fast-forward; anything else needs a
  # human decision about which history to keep.
  if $remote_branch_exists; then
    remote_tip=$(git rev-parse "refs/remotes/${push_remote}/${current_branch}")
    if ! git merge-base --is-ancestor "$remote_tip" HEAD; then
      echo "" >&2
      echo "error: push to ${push_remote}/${current_branch} is not a" >&2
      echo "       fast-forward, and PR #${pr_num} is out of draft." >&2
      if git merge-base --is-ancestor HEAD "$remote_tip"; then
        echo "       The remote branch is ahead of you. Integrate it:" >&2
        echo "         git merge --ff-only ${push_remote}/${current_branch}" >&2
      else
        echo "       Your branch and the remote have diverged (a rebase or" >&2
        echo "       amend happened locally). Integrate with a merge:" >&2
        echo "         git merge ${push_remote}/${current_branch}" >&2
        echo "       A merge commit on the task branch is harmless -- the PR" >&2
        echo "       is squash-merged, so it never reaches ${base_branch}." >&2
      fi
      echo "       Or re-run with -f to rewrite the branch anyway; that" >&2
      echo "       marks reviewers' existing line comments as outdated." >&2
      exit 4
    fi
  fi

  # Report drift instead of silently correcting it: picking up the base is a
  # merge the author should make deliberately, not a side effect of pushing.
  behind=$(git rev-list --count "HEAD..${UPSTREAM_REMOTE}/${base_branch}" \
             2>/dev/null || echo 0)
  if (( behind > 0 )); then
    echo ">>> note: branch is ${behind} commit(s) behind" \
         "${UPSTREAM_REMOTE}/${base_branch}."
    echo "    To pick up the base: git merge ${UPSTREAM_REMOTE}/${base_branch}"
  fi

  # Lint the branch's own changes. Without the rebase, linting against the
  # moving base branch would drag in everything it gained since we forked.
  lint_rev=$(git merge-base "${UPSTREAM_REMOTE}/${base_branch}" HEAD)
else
  # Integrate any commits already on the remote branch (e.g. pushed from
  # another machine or another agent), then rebase onto the latest
  # upstream/<base> so the push lands on top of a fresh base.
  if $remote_branch_exists; then
    echo ">>> rebasing onto ${push_remote}/${current_branch}"
    if ! git rebase "${push_remote}/${current_branch}"; then
      echo "" >&2
      echo "error: rebase onto ${push_remote}/${current_branch} failed." >&2
      echo "       Resolve the conflicts, 'git add' the resolved files," >&2
      echo "       run 'git rebase --continue', then re-run this script." >&2
      exit 2
    fi
  fi

  echo ">>> rebasing onto ${UPSTREAM_REMOTE}/${base_branch}"
  if ! git rebase "${UPSTREAM_REMOTE}/${base_branch}"; then
    echo "" >&2
    echo "error: rebase onto ${UPSTREAM_REMOTE}/${base_branch} failed." >&2
    echo "       Resolve the conflicts, 'git add' the resolved files," >&2
    echo "       run 'git rebase --continue', then re-run this script." >&2
    exit 2
  fi

  lint_rev="${UPSTREAM_REMOTE}/${base_branch}"
fi

# Ensure the linter is happy. Never push if lint isn't clean.
# Resolve the repo root so `build-support/lint.sh` works regardless of
# the caller's cwd (a subdirectory invocation otherwise hits "no such file").
repo_root=$(git rev-parse --show-toplevel)
echo ">>> running ${repo_root}/build-support/lint.sh --rev ${lint_rev}"
if ! "${repo_root}/build-support/lint.sh" --rev "$lint_rev"; then
  echo "" >&2
  echo "error: lint failed. Fix issues as a NEW commit" >&2
  echo "       (do not amend a pushed commit), then re-run this script." >&2
  exit 3
fi

# Capture the pre-push remote SHA (empty on first push) so we can list the
# new commits afterwards and remind the user/agent to keep the PR summary in sync.
pre_push_sha=$(git rev-parse --verify --quiet \
                 "${push_remote}/${current_branch}" 2>/dev/null || true)

if $append_only; then
  echo ">>> pushing ${current_branch} -> ${push_remote} (${push_target_desc})"
  git push -u "$push_remote" HEAD
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
