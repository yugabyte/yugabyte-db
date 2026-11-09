---
name: review-backport
description: >-
  Review a YugabyteDB backport -- a GitHub PR or a Phorge diff -- by
  comparing it against its original commit. Use when the user provides a
  backport PR (number or URL, title starting `[BACKPORT <branch>]`) or a
  Phorge backport diff ID (e.g. D51405) and wants to verify the backport
  is correct.
---

# Review Backport

Review a YugabyteDB backport by comparing it to the original commit and identifying any differences.

For the review *policy* (what counts as a backport, what to flag vs. ignore, byte-identical-hunk handling, and the `## Merge conflicts` convention), see the **Backport PRs** section of `@../../../REVIEW.md`. This skill covers the *mechanics* of fetching and comparing the diffs and tracing unexplained code back to its origin on master.

Only fetching the backport itself depends on where it lives. The original is a commit on master, so compare against `git show <sha>` and trace history with `git`, whether the original landed as a PR or via Phorge.

## Prerequisites

Run `git fetch <upstream>` first, where `<upstream>` is the remote pointing at `yugabyte/yugabyte-db` (`git remote -v`), so the original commit and the target release branch are available locally.

- **GitHub PR:** `gh` installed and authenticated (`gh auth status`).
- **Phorge diff:** the Phorge MCP server. If it is not available, instruct the user to set it up:
  1. **Generate a Conduit API token.** In Phorge, click on your profile in the top right → Settings → Conduit API Tokens → Generate Token.
  2. **Add the Phorge MCP server** to the agent's MCP config (e.g. `~/.cursor/mcp.json`, or `claude mcp add` for Claude Code), replacing the API token with the one you just generated:

     ```json
     {
       "mcpServers": {
         "phorge": {
           "command": "npx",
           "args": ["@freelancercom/phabricator-mcp@latest"],
           "env": {
             "PHABRICATOR_URI": "https://phorge.dev.yugabyte.com/",
             "PHABRICATOR_API_TOKEN": "api-XXXXXXXXXXXXXXXX"
           }
         }
       }
     }
     ```
  3. **Restart the agent** to pick up the new MCP server configuration.

## Workflow

### Step 1: Fetch the backport

Confirm the title starts with `[BACKPORT <version>]`, and save the title, description, target branch, and diff.

**GitHub PR** (`#12345` or a PR URL):

```
gh pr view <n> --repo yugabyte/yugabyte-db --json title,body,baseRefName,state,commits
gh pr diff <n> --repo yugabyte/yugabyte-db > /tmp/claude/backport-<n>.diff
```

The target branch is `baseRefName`.

**Phorge diff** (`D51405`):

```
Tool: phabricator_revision_search
Args: { "constraints": { "ids": [<numeric_id>] }, "limit": 1 }
```

The numeric ID is the diff number without the "D" prefix (e.g., D51405 → 51405). Save the revision's `phid` and `summary`. Then get its latest diff snapshot and the raw patch:

```
Tool: phabricator_diff_search
Args: {
  "constraints": { "revisionPHIDs": ["<revision_phid>"] },
  "order": "newest",
  "limit": 1,
  "attachments": { "commits": true }
}

Tool: phabricator_diff_raw
Args: { "diffID": <diff_id> }
```

The target branch is in the diff's `refs` attachment.

Either way, check that the version in the title matches the target branch (e.g. `[BACKPORT 2024.2]` targeting `2024.2`). If they don't match, flag it — the backport may be targeting the wrong branch.

### Step 2: Parse the description for references

1. **Original commit**: look for a line `Original commit: <sha> / #<PR>` (original merged as a GitHub PR) or `Original commit: <sha> / D<number>` (original landed via Phorge). If it is missing, ask the user for the original commit or PR/diff.

2. **Additional changes applied**: look for references to other changes, such as:
   - "Apply small diff changes from D51357 as well."
   - "Also includes changes from #34567."
   - Any other `D<number>`, `#<PR>`, or commit SHA that is not the original.

   Collect them all.

3. **Documented conflict resolutions**: the `## Merge conflicts` section of a PR body, or the `Merge conflicts:` block in a Phorge summary.

### Step 3: Get the original and any additional changes

```
git show --format='%H%n%an%n%s%n%n%b' <original_sha> > /tmp/claude/original-<sha>.diff
```

Confirm the SHA is on master (`git merge-base --is-ancestor <sha> <upstream>/master`) and that its subject matches the backport title minus the `[BACKPORT ...]` tag.

For each additional reference, get its diff the same way: find the merged commit for a PR (`gh pr view <n> --json mergeCommit`) or Phorge diff (`git log <upstream>/master -n1 --format=%H --grep='^Differential Revision: .*/D<number>$'`) and `git show` it. For a Phorge revision that never landed, fetch it as in Step 1. These changes are **expected** to appear in the backport but not in the original.

### Step 4: Compare and review

Compare the backport diff against the original diff. Categorize all differences:

#### 4a. File-level comparison

List files that appear in one diff but not the other.

#### 4b. Hunk-level comparison

For files present in both diffs, compare the actual changes (added/removed lines). Ignore differences in:
- Context line offsets (line numbers shift due to different branch bases)
- Diff metadata headers (`diff --git`, `index` lines, `---`/`+++` paths)

Focus on differences in the actual `+`/`-` lines within hunks.

#### 4c. Classify differences

For each difference found, classify it as:

1. **From an additional referenced change** — changes that match a change mentioned in the description (e.g., D51357 or #34567). These are expected.
2. **Explained in the description** — a documented merge-conflict entry, or a sentence that describes and justifies the difference (e.g., "removed function X because it doesn't exist on 2024.1", "adapted call to use older API"). These are expected and do not need to be flagged, unless the resolution looks logically wrong.
3. **Conflict resolution** — changes that look like manual conflict resolution during cherry-pick/backport but are not documented. Flag for review.
4. **Unexplained difference** — changes that don't match any referenced change, aren't explained in the description, and don't look like conflict resolution. Flag as potentially problematic.

### Step 5: Trace unexplained differences back to their origin on master

Skip this step for differences that are already classified as "explained in the description" — those do not need origin tracing.

When the backport contains added code that is NOT in the original diff, NOT from any referenced additional change, and NOT explained in the description, it was most likely pulled in accidentally during the cherry-pick. This happens when the original commit on master has trailing context (functions defined nearby in the same file) that doesn't exist on the target release branch. Git's conflict resolution during cherry-pick can drag in these unrelated functions.

For each block of unexplained new code:

1. **Identify a distinctive function or symbol name** from the unexplained code (e.g., `YbNewSample`, `yb_maybe_test_fail_ddl`).

2. **Verify it doesn't exist on the target branch**:
   ```
   git grep -n '<symbol>' <upstream>/<target_branch> -- <file_path>
   ```
   If it returns nothing, the code is not part of the target branch and should not be in the backport.

3. **Confirm it exists on master before the original commit**:
   ```
   git grep -n '<symbol>' <original_sha>~1 -- <file_path>
   ```
   If found, this confirms the code pre-existed on master and was adjacent to the original change.

4. **Find the commit that introduced it on master**:
   ```
   git log -S'<symbol>' --reverse --format='%H %an %s' <original_sha>~1 -- <file_path>
   ```
   The first line is the introducing commit (add `--follow` if the file was renamed).

5. **Report the origin**: include the introducing commit hash, its PR (`(#NNNNN)` at the end of the subject) or Phorge diff ID (`Differential Revision:` line in the body), title, and author. Explain that these changes are unrelated to the backport and were pulled in during cherry-pick conflict resolution. They should be removed from the backport.

### Step 6: Produce the review report

Output a structured review with these sections:

**Backport Summary**
- Backport: #<PR> or D<id> — <title>
- Original: <sha> / #<PR> or D<id> — <title>
- Target branch: <branch>
- Additional changes included: list (or "None")

**File Comparison**
- Files in both diffs: list
- Files only in backport: list (with explanation)
- Files only in original: list (with explanation — this might indicate a problem)

**Change Comparison**
- Identical changes: list of files with no meaningful diff differences
- Expected differences (from additional changes or documented conflicts): describe which changes come from where
- Differences requiring review: for each difference, quote the relevant `+`/`-` lines from both the original and backport diffs so the reviewer can see exactly what changed. Do not just describe the difference in prose — always include the actual code context.

**Unrelated Code Pulled In From Master** (if any)
- For each block of unexplained code: the file, the functions/symbols involved, the originating commit on master (hash, PR or diff ID, title, author), and confirmation that this code does not exist on the target branch
- Recommendation to remove these from the backport

**Assessment**
- Overall verdict: Clean backport / Minor differences / Needs attention
- Specific concerns, if any

Report in chat. Do not post comments on the PR or Phorge diff unless the user asks.

## Notes

- For large diffs, focus comparison on the `+`/`-` lines rather than context.
- Line number offsets in hunks (`@@ -a,b +c,d @@`) will almost always differ between the original and backport — this is normal and expected.
- Some backports are cherry-picks that apply cleanly; others require conflict resolution. Both are valid.
- When the description mentions additional changes, always fetch and account for those changes before flagging differences.
- Unexplained code in a backport is almost always from unrelated commits on master that were adjacent to the original change in the same file. During cherry-pick, git's conflict resolution can pull in these neighboring functions as context. Always trace such code back to its origin on master rather than just flagging it as "unexplained."
