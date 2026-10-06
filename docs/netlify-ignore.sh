#!/usr/bin/env bash
# Netlify runs this from the site's base directory (docs/) before every build.
# Exit 0 cancels the build; any non-zero status lets it proceed.
#
# When we cannot establish what a build would cover we let it run: a redundant
# preview is cheaper than a missing one.

set -uo pipefail

REPO=${DOCS_REPO_SLUG:-yugabyte/yugabyte-db}
# GitHub caps this endpoint at 3000 files, so a full 30th page means the list
# was truncated and we can no longer rule out docs edits.
MAX_PAGES=30

build() { echo "netlify-ignore: $1; building."; exit 1; }
skip()  { echo "netlify-ignore: $1; skipping build."; exit 0; }

if [[ ${PULL_REQUEST:-false} == true ]]; then
  # CACHED_COMMIT_REF is the last commit built for this context, which for a
  # deploy preview is an unrelated commit on the production branch. Diffing
  # against it reports every docs commit that landed between that commit and
  # this branch's base, so it flags PRs that never touched docs. Ask GitHub what
  # the PR itself changed instead.
  [[ -n ${REVIEW_ID:-} ]] || build "deploy preview with no REVIEW_ID"

  curl_args=(--silent --show-error --fail --max-time 30
             -H 'Accept: application/vnd.github+json')
  [[ -n ${GITHUB_TOKEN:-} ]] && curl_args+=(-H "Authorization: Bearer $GITHUB_TOKEN")

  for ((page = 1; page <= MAX_PAGES; page++)); do
    payload=$(curl "${curl_args[@]}" \
      "https://api.github.com/repos/$REPO/pulls/$REVIEW_ID/files?per_page=100&page=$page") \
      || build "could not list the files in PR #$REVIEW_ID"

    # previous_filename too, so renaming a page out of docs/ still rebuilds.
    paths=$(grep -oE '"(previous_)?filename"[[:space:]]*:[[:space:]]*"[^"]*"' <<<"$payload" |
              sed 's/.*"\(.*\)"$/\1/')
    grep -q '^docs/' <<<"$paths" && build "PR #$REVIEW_ID edits docs/"

    (( $(grep -o '"filename"' <<<"$payload" | wc -l) < 100 )) &&
      skip "PR #$REVIEW_ID does not touch docs/"
  done
  build "PR #$REVIEW_ID has a truncated file list ($((MAX_PAGES * 100))+ files)"
fi

# Production and branch deploys advance along a single branch, so the cached
# commit is an ancestor of this one and diffing the two is accurate.
[[ -n ${CACHED_COMMIT_REF:-} ]] || build "no CACHED_COMMIT_REF to compare against"
[[ ${CACHED_COMMIT_REF} != ${COMMIT_REF:-} ]] ||
  build "$COMMIT_REF was already the last build, so this is a rebuild"
git cat-file -e "${CACHED_COMMIT_REF}^{commit}" 2>/dev/null ||
  build "CACHED_COMMIT_REF $CACHED_COMMIT_REF is not in this clone"
git diff --quiet "$CACHED_COMMIT_REF" "$COMMIT_REF" -- . ||
  build "docs/ changed since $CACHED_COMMIT_REF"
skip "no docs/ changes since $CACHED_COMMIT_REF"
