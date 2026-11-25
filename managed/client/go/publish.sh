#!/usr/bin/env bash
# Publish the generated Go clients (client/go/v1, client/go/v2) to
# github.com/yugabyte/platform-go-client as a draft PR.
#
# Run `sbt swaggerGen` first. Then:
#   ./publish.sh [--dry-run] [path-to-platform-go-client-clone]
#
# The clone defaults to $PLATFORM_GO_CLIENT_DIR or ~/code/platform-go-client, and its origin
# must be yugabyte/platform-go-client. The script works in a temporary worktree of
# origin/master, so local files in the clone are not touched. The copy is a mirror: files the
# generator no longer writes are deleted from the client repo, and the PR body lists them. A
# deleted file that is not a generated model, API or doc stops the publish.
set -euo pipefail

DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=true
  shift
fi

GEN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANAGED_DIR="$(cd "$GEN_DIR/../.." && pwd)"
CLIENT_REPO="${1:-${PLATFORM_GO_CLIENT_DIR:-$HOME/code/platform-go-client}}"
# The GitHub user that reviews each client PR. GitHub cannot request a review from the PR
# author, so a PR opened by this user gets no review request.
CLIENT_REVIEWER="${CLIENT_REVIEWER:-Deepti-yb}"

die() {
  echo "ERROR: $*" >&2
  exit 1
}

# The generated trees must exist and come from the current generator config.
for v in v1 v2; do
  [[ -f "$GEN_DIR/$v/client.go" && -f "$GEN_DIR/$v/.openapi-generator/FILES" ]] \
    || die "$GEN_DIR/$v is not generated. Run sbt swaggerGen."
done

# The generator does not delete files that it no longer writes. Only the files in
# .openapi-generator/FILES come from the last generation.
generated_files() {
  tr '\n' '\0' < "$GEN_DIR/$1/.openapi-generator/FILES"
}
grep -q '^package ywclient$' "$GEN_DIR/v1/client.go" \
  || die "v1 was generated with an old config (package is not ywclient). Run sbt swaggerGen."
grep -qx 'module github.com/yugabyte/platform-go-client' "$GEN_DIR/v1/go.mod" \
  || die "v1/go.mod has the wrong module path. Run sbt swaggerGen."

# Passes on only the paths that exist, so find does not fail. The uncommitted-changes check
# reports a deleted source, and the copy step reports a missing generated file.
existing() {
  local f
  while IFS= read -r -d '' f; do
    [[ -e "$f" ]] && printf '%s\0' "$f"
  done
  return 0
}

# The generated trees and their input specs are gitignored, so git status cannot see that they
# are stale. A checkout, merge or rebase rewrites the files it changes, so a tracked source newer
# than the spec, or a generated file older than it, means the client does not match HEAD.
stale_client() {
  local v="$1" spec="$2"; shift 2
  [[ -f "$spec" ]] || { echo "$spec is missing"; return; }
  local newer
  newer="$(cd "$MANAGED_DIR" && git ls-files -z -- "$@" | existing \
    | xargs -0 -r sh -c 'find "$@" -newer "$0" -print -quit' "$spec")"
  [[ -z "$newer" ]] || { echo "${newer%%$'\n'*} is newer than $spec"; return; }
  newer="$(cd "$GEN_DIR/$v" && generated_files "$v" | existing \
    | xargs -0 -r sh -c 'find "$@" -name "*.go" ! -newer "$0" -print -quit' "$spec")"
  [[ -z "$newer" ]] || echo "$newer is older than $spec"
}
STALE="$(stale_client v1 "$MANAGED_DIR/src/main/resources/swagger-all.json" \
  src/main/java src/main/resources/routes src/main/resources/v1.routes swagger/src)"
[[ -z "$STALE" ]] || STALE="$STALE. Run sbt swaggerGen."
V2_STALE="$(stale_client v2 "$MANAGED_DIR/target/openapi/src/main/java/public/openapi.json" \
  src/main/resources/openapi)"
# sbt rebuilds the v2 spec only when its inputs change content, not mtime. cleanV2ServerStubs
# forces the rebuild.
[[ -z "$V2_STALE" ]] || STALE+=" $V2_STALE. Run sbt \"cleanV2ServerStubs; swaggerGen\"."
# The generator's templates, config, version (plugins.sbt) and settings (build.sbt) change the
# client without changing the spec. One newer than the generated client.go means the client
# predates it. swaggerGen regenerates both clients.
for v in v1 v2; do
  cfg=openapi-go-config.json
  [[ "$v" == v1 ]] || cfg=openapi-go-config-v2.json
  newer="$(cd "$MANAGED_DIR" && git ls-files -z -- client/go/templates "client/go/$cfg" \
    build.sbt project/plugins.sbt | existing \
    | xargs -0 -r sh -c 'find "$@" -newer "$0" -print -quit' "$GEN_DIR/$v/client.go")"
  [[ -z "$newer" ]] || STALE+=" $newer is newer than the $v client. Run sbt swaggerGen."
done
STALE="${STALE# }"
if [[ -n "$STALE" ]]; then
  $DRY_RUN || die "The generated client is stale: $STALE"
  echo "WARNING: the generated client is stale: $STALE" >&2
fi

git -C "$CLIENT_REPO" rev-parse --git-dir >/dev/null 2>&1 \
  || die "$CLIENT_REPO is not a git clone."
git -C "$CLIENT_REPO" remote get-url origin | grep -q 'yugabyte/platform-go-client' \
  || die "The origin of $CLIENT_REPO is not yugabyte/platform-go-client."
# cleanup runs from /, so a relative clone path must become absolute.
CLIENT_REPO="$(cd "$CLIENT_REPO" && pwd)"

# The commit names the yugabyte-db SHA, so the generated code must match it.
# A fixed length: the default abbreviation depends on the clone, and the once-per-SHA check
# below matches the branch name.
SRC_SHA="$(git -C "$MANAGED_DIR" rev-parse --short=12 HEAD)"
if [[ -n "$(git -C "$MANAGED_DIR" status --porcelain -- .)" ]]; then
  $DRY_RUN || die "managed/ has uncommitted changes, so the client may not match $SRC_SHA."
  echo "WARNING: managed/ has uncommitted changes." >&2
fi
if [[ -z "$(git -C "$MANAGED_DIR" branch -r --contains HEAD 2>/dev/null)" ]]; then
  echo "WARNING: $SRC_SHA is not on any remote branch yet." >&2
fi

DATE="$(date -u +%Y-%m-%d)"
BRANCH="client-${DATE}-${SRC_SHA}"
$DRY_RUN && BRANCH="${BRANCH}-dryrun"

# One publish per SHA, on any day. A branch without a PR means an earlier run failed after the
# push. A dry run warns, so it reports what the real run will do.
HEADS="$(git -C "$CLIENT_REPO" ls-remote --heads origin)" \
  || die "Cannot list the branches of platform-go-client."
# Match the branch's SHA suffix as a prefix of the full SHA, so a branch named with another
# abbreviation length is found too.
FULL_SHA="$(git -C "$MANAGED_DIR" rev-parse HEAD)"
EXISTING="$(awk -v full="$FULL_SHA" '{ b = $2; sub("^refs/heads/", "", b) }
  b ~ /^client-.*-[0-9a-f]+$/ { s = b; sub(/.*-/, "", s)
    if (length(s) >= 7 && index(full, s) == 1) { print b; exit } }' <<< "$HEADS")"
if [[ -n "$EXISTING" ]]; then
  PR_URL="$(gh pr list --repo yugabyte/platform-go-client --head "$EXISTING" --state all \
    --json url --jq '.[0].url // empty')" \
    || die "Cannot list platform-go-client PRs with gh, so cannot check branch $EXISTING."
  if [[ -n "$PR_URL" ]]; then
    MSG="$SRC_SHA was already published as $PR_URL."
  else
    MSG="Branch $EXISTING exists on origin without a PR. Open its PR with gh pr create, or"
    MSG+=" delete the branch, then run again."
  fi
  $DRY_RUN || die "$MSG"
  echo "WARNING: the real run will stop: $MSG" >&2
fi

WORKTREE=""
GOWORK_DIR=""
KEEP_BRANCH=false
cleanup() {
  cd /
  if [[ -n "$WORKTREE" ]]; then
    git -C "$CLIENT_REPO" worktree remove --force "$WORKTREE" >/dev/null 2>&1 || true
    rm -rf "$WORKTREE"
  fi
  [[ -z "$GOWORK_DIR" ]] || rm -rf "$GOWORK_DIR"
  if ! $KEEP_BRANCH; then
    git -C "$CLIENT_REPO" branch -D "$BRANCH" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT
WORKTREE="$(mktemp -d)"
GOWORK_DIR="$(mktemp -d)"

git -C "$CLIENT_REPO" fetch -q origin master
# -B resets a local branch left by an earlier run of the same SHA.
git -C "$CLIENT_REPO" worktree add -q -B "$BRANCH" "$WORKTREE" origin/master

# Stage only the generated files, so files left from an earlier generation are not published.
STAGE="$GOWORK_DIR/stage"
for v in v1 v2; do
  mkdir -p "$STAGE/$v"
  (cd "$GEN_DIR/$v" && generated_files "$v" | rsync -a --from0 --files-from=- . "$STAGE/$v/") \
    || die "Cannot copy the files in $v/.openapi-generator/FILES. Run sbt swaggerGen."
done

# v1 is the repo root. Keep the repo's own .gitignore and LICENSE, and v2/ (mirrored next).
# Generator metadata is not published.
META=(--exclude .openapi-generator --exclude .openapi-generator-ignore)
rsync -a --delete "${META[@]}" --exclude /.git --exclude /.gitignore --exclude /LICENSE \
  --exclude /v2 "$STAGE/v1/" "$WORKTREE/"
rsync -a --delete "${META[@]}" "$STAGE/v2/" "$WORKTREE/v2/"

# The mirror may delete only generated files: models, APIs, their docs and tests. A deleted file
# of another kind was added to the client repo by hand, so the publish stops.
DELETED="$GOWORK_DIR/deleted.txt"
git -C "$WORKTREE" ls-files --deleted >"$DELETED" || die "Cannot list the deleted files."
NOT_GENERATED="$(awk '{ p = $0; sub(/^v2\//, "", p) }
  p !~ /^((model|api)_[a-z0-9_]+\.go|docs\/[A-Za-z0-9_]+\.md|test\/api_[a-z0-9_]+_test\.go)$/' \
  "$DELETED")"
if [[ -n "$NOT_GENERATED" ]]; then
  MSG="The mirror deletes files the generator does not write: ${NOT_GENERATED//$'\n'/ }."
  MSG+=" Exclude them from the rsync above, or delete them in the client repo first."
  $DRY_RUN || die "$MSG"
  echo "WARNING: the real run will stop: $MSG" >&2
fi

(cd "$WORKTREE" && go build ./...) || die "v1 client does not build."
(cd "$WORKTREE/v2" && go build ./...) || die "v2 client does not build."

# List the Go API changes that break callers, against the client on origin/master. A break is
# reported, not fatal, and the list goes in the PR body. A publish stops when apidiff cannot
# install or read a client, so a PR never goes out without its break list.
APIDIFF_VERSION="v0.0.0-20261121134650-e1094eeca1b5"
APIDIFF_DIR="$GOWORK_DIR/apidiff"
APIDIFF="$APIDIFF_DIR/bin/apidiff"
API_BREAKS="$APIDIFF_DIR/breaks.txt"
mkdir -p "$APIDIFF_DIR/bin" "$APIDIFF_DIR/old"
GOBIN="$APIDIFF_DIR/bin" go install "golang.org/x/exp/cmd/apidiff@$APIDIFF_VERSION" \
  || die "Cannot install apidiff."
git -C "$CLIENT_REPO" archive origin/master | tar -x -C "$APIDIFF_DIR/old"
: >"$API_BREAKS"
for m in v1 v2; do
  sub=""
  [[ "$m" == v1 ]] || sub="$m"
  (cd "$APIDIFF_DIR/old/$sub" && GOWORK=off "$APIDIFF" -w "$APIDIFF_DIR/$m.api" .) \
    || die "apidiff cannot read the $m client on origin/master."
  out="$(cd "$WORKTREE/$sub" && GOWORK=off "$APIDIFF" -incompatible "$APIDIFF_DIR/$m.api" .)" \
    || die "apidiff cannot read the new $m client."
  [[ -z "$out" ]] || printf '%s:\n%s\n' "$m" "$out" >>"$API_BREAKS"
done
if [[ -s "$API_BREAKS" ]]; then
  echo "WARNING: $(grep -c '^- ' "$API_BREAKS") Go API changes break callers of the client:" >&2
  cat "$API_BREAKS" >&2
else
  echo "No Go API changes that break callers."
fi

# The consumers must still build against the new client. A temporary go.work points them at
# the generated code without changing their go.mod. A yba-cli break stops the publish; the
# Terraform provider is checked when its clone exists, and a break there is a warning.
GOWORK_FILE="$GOWORK_DIR/go.work"
GO_LOG="$GOWORK_DIR/go.log"
# check_consumer <name> <dir> <test-regexp>: returns non-zero and prints the go output on failure.
check_consumer() {
  local name="$1" dir="$2"
  echo "Checking $name ($(git -C "$dir" rev-parse --abbrev-ref HEAD 2>/dev/null)" \
    "at $(git -C "$dir" rev-parse --short HEAD 2>/dev/null)) against the new client..."
  rm -f "$GOWORK_FILE"
  if ! (cd "$GOWORK_DIR" && GOWORK=off go work init "$dir" "$WORKTREE" "$WORKTREE/v2") \
    >"$GO_LOG" 2>&1; then
    cat "$GO_LOG" >&2
    echo "Cannot create a Go workspace for $name." >&2
    return 1
  fi
  # -vet=off: vet's printf check fails on the existing logrus.Fatalf(formatter.Colorize(...))
  # calls, which has nothing to do with the client. -run "$3" picks the tests to run.
  if ! (cd "$dir" && GOWORK="$GOWORK_FILE" go build ./... \
    && GOWORK="$GOWORK_FILE" go test -count=1 -vet=off -run "$3" ./...) >"$GO_LOG" 2>&1; then
    grep -vE '^(ok|\?) ' "$GO_LOG" >&2
    return 1
  fi
}
# yba-cli runs all its unit tests; the Terraform provider only compiles its tests.
check_consumer yba-cli "$MANAGED_DIR/yba-cli" '.' \
  || die "yba-cli does not build or pass its tests against the new client. Fix yba-cli first."
TF_DIR="${TERRAFORM_PROVIDER_YBA_DIR:-$HOME/code/terraform-provider-yba}"
if [[ -f "$TF_DIR/go.mod" ]]; then
  # The provider bumps the client on its own schedule, so a break is reported, not fatal. The
  # check builds the local clone as it is, so local edits can also cause a failure.
  check_consumer terraform-provider-yba "$TF_DIR" '^$' \
    || echo "WARNING: terraform-provider-yba does not build against the new client (errors" \
      "above). Fix it when it bumps the client." >&2
else
  echo "WARNING: $TF_DIR not found; terraform-provider-yba was not checked." >&2
fi

cd "$WORKTREE"
git add -A
if git diff --cached --quiet; then
  echo "No changes: platform-go-client already matches $SRC_SHA."
  exit 0
fi
git diff --cached --stat | tail -1
git commit -q -m "Client generated from yugabyte-db ${SRC_SHA} on ${DATE}"

if $DRY_RUN; then
  KEEP_BRANCH=true
  echo "Dry run: committed on local branch $BRANCH in $CLIENT_REPO; not pushed."
  echo "Delete it with: git -C $CLIENT_REPO branch -D $BRANCH"
  exit 0
fi

BODY_FILE="$GOWORK_DIR/body.md"
{
  echo "Generated by managed/client/go/publish.sh from yugabyte-db ${SRC_SHA}."
  if [[ -s "$DELETED" ]]; then
    echo
    echo "Deleted $(wc -l <"$DELETED" | tr -d ' ') files that the generator no longer writes:"
    echo '```'
    head -n 500 "$DELETED"
    echo '```'
  fi
  if [[ -s "$API_BREAKS" ]]; then
    echo
    echo "Go API changes that break callers (apidiff):"
    echo '```'
    head -c 50000 "$API_BREAKS"
    echo '```'
  fi
} >"$BODY_FILE"

git push -q -u origin "$BRANCH"
KEEP_BRANCH=true
# GitHub refuses a review request from the PR author.
REVIEWER_ARGS=()
if [[ "$(gh api user -q .login)" != "$CLIENT_REVIEWER" ]]; then
  REVIEWER_ARGS=(--reviewer "$CLIENT_REVIEWER")
fi
gh pr create --draft --repo yugabyte/platform-go-client --base master --head "$BRANCH" \
  --title "Client generated from yugabyte-db ${SRC_SHA} on ${DATE}" --body-file "$BODY_FILE" \
  ${REVIEWER_ARGS[@]+"${REVIEWER_ARGS[@]}"}
