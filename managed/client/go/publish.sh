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
# generator no longer writes are deleted from the client repo.
set -euo pipefail

DRY_RUN=false
if [[ "${1:-}" == "--dry-run" ]]; then
  DRY_RUN=true
  shift
fi

GEN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANAGED_DIR="$(cd "$GEN_DIR/../.." && pwd)"
CLIENT_REPO="${1:-${PLATFORM_GO_CLIENT_DIR:-$HOME/code/platform-go-client}}"

die() {
  echo "ERROR: $*" >&2
  exit 1
}

# The generated trees must exist and come from the current generator config.
for v in v1 v2; do
  [[ -f "$GEN_DIR/$v/client.go" ]] || die "$GEN_DIR/$v is not generated. Run sbt swaggerGen."
done
grep -q '^package ywclient$' "$GEN_DIR/v1/client.go" \
  || die "v1 was generated with an old config (package is not ywclient). Run sbt swaggerGen."
grep -qx 'module github.com/yugabyte/platform-go-client' "$GEN_DIR/v1/go.mod" \
  || die "v1/go.mod has the wrong module path. Run sbt swaggerGen."

git -C "$CLIENT_REPO" rev-parse --git-dir >/dev/null 2>&1 \
  || die "$CLIENT_REPO is not a git clone."
git -C "$CLIENT_REPO" remote get-url origin | grep -q 'yugabyte/platform-go-client' \
  || die "The origin of $CLIENT_REPO is not yugabyte/platform-go-client."

# The commit names the yugabyte-db SHA, so the generated code must match it.
SRC_SHA="$(git -C "$MANAGED_DIR" rev-parse --short HEAD)"
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
EXISTING="$(git -C "$CLIENT_REPO" ls-remote --heads origin \
  | awk '{print $2}' | sed 's#^refs/heads/##' | grep -E "^client-.*-${SRC_SHA}$" | head -1)" || true
if [[ -n "$EXISTING" ]]; then
  PR_URL="$(gh pr list --repo yugabyte/platform-go-client --head "$EXISTING" --state all \
    --json url --jq '.[0].url // empty' 2>/dev/null)" || true
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

# v1 is the repo root. Keep the repo's own .gitignore and LICENSE, and v2/ (mirrored next).
# Generator metadata is not published.
META=(--exclude .openapi-generator --exclude .openapi-generator-ignore)
rsync -a --delete "${META[@]}" --exclude /.git --exclude /.gitignore --exclude /LICENSE \
  --exclude /v2 "$GEN_DIR/v1/" "$WORKTREE/"
rsync -a --delete "${META[@]}" "$GEN_DIR/v2/" "$WORKTREE/v2/"

(cd "$WORKTREE" && go build ./...) || die "v1 client does not build."
(cd "$WORKTREE/v2" && go build ./...) || die "v2 client does not build."

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

git push -q -u origin "$BRANCH"
KEEP_BRANCH=true
gh pr create --draft --repo yugabyte/platform-go-client --base master --head "$BRANCH" \
  --title "Client generated from yugabyte-db ${SRC_SHA} on ${DATE}" \
  --body "Generated by managed/client/go/publish.sh from yugabyte-db ${SRC_SHA}.
Files the generator no longer writes are deleted."
