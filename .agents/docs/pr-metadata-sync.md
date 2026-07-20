# Updating PR body and title from the command line

Mechanics for keeping a PR's summary and title in sync after pushing follow-up commits.
The policy for *when* to update is in `src/AGENTS.md` ("Git workflow on branches with an open PR").

## Body

Edit the cached body file with the Edit tool, not `python3`/`sed`/`awk` shell-outs
(`python3 -c '...'` is intentionally not allowlisted — too broad — and will prompt every time):

1. Fetch the current body (allowed via `gh pr view`):
   ```bash
   gh pr view <num> -R <repo> --json body -q .body > /tmp/claude/pr-body-<num>.md
   ```
2. Modify it with the Edit tool (allowed via `Edit(/tmp/claude/**)`).
3. Patch it back (both commands allowed):
   ```bash
   jq -Rs '{body: .}' < /tmp/claude/pr-body-<num>.md | \
     gh api -X PATCH /repos/<repo>/pulls/<num> --input -
   ```

`git-push.sh` prints this template after every push.

## Title

```bash
gh api -X PATCH /repos/<repo>/pulls/<num> -f title='<new title>'
```

Allowed via the same scoped pulls PATCH rule the body update uses.
The title must still match `[<issue>] <Component>: <Title>`.
