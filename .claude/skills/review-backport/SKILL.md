---
name: review-backport
description: >-
  Review YugabyteDB backport diffs by comparing them against their original
  commits/diffs. Use when the user provides a Phorge backport diff ID
  (e.g. D51405) and wants to verify the backport is correct, or when
  reviewing backport revisions.
---

# Review Backport

Review a YugabyteDB backport diff by comparing it to the original commit/diff and identifying any differences.

For the review *policy* (what counts as a backport, what to flag vs. ignore, byte-identical-hunk handling, and the `## Merge conflicts` convention), see the **Backport PRs** section of `@../../../.gemini/styleguide.md`. This skill covers the *mechanics* of fetching and comparing Phorge diffs and tracing unexplained code back to its origin on master.

## Prerequisites

This skill requires the Phorge MCP server. If the `user-phorge` MCP server is not available, instruct the user to set it up:

1. **Generate a Conduit API token.** In Phorge, click on your profile in the top right → Settings → Conduit API Tokens → Generate Token.

2. **Add the Phorge MCP server to Cursor.** Create or edit `~/.cursor/mcp.json` with these contents, replacing the API token with the one you just generated:

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

3. **Restart AI agent** to pick up the new MCP server configuration.

## Workflow

### Step 1: Fetch the backport revision

Use the `user-phorge` MCP server to get the backport revision details:

```
Tool: phabricator_revision_search
Args: { "constraints": { "ids": [<numeric_id>] }, "limit": 1 }
```

The numeric ID is the diff number without the "D" prefix (e.g., D51405 → 51405).

Confirm the title starts with `[BACKPORT <version>]`. Save the revision's `phid` and `summary`.

Optionally, verify the version in the title corresponds to a real branch. The diff metadata includes a `refs` attachment with the target branch. Cross-check that the version string (e.g., `2024.1.3`) matches the branch name (e.g., `origin/2024.1.3`). If they don't match, flag it — the backport may be targeting the wrong branch.

### Step 2: Parse the summary for references

Extract from the summary field:

1. **Original commit/diff**: Look for a line matching:
   `Original commit: <commit_hash> / D<number>`
   This gives both the git commit hash and the original Phorge revision ID.

2. **Additional diffs applied**: Look for references to other diffs in the summary, such as:
   - "Apply small diff changes from D51357 as well."
   - "Also includes changes from D12345."
   - Any mention of `D<number>` that is not the original diff.

   Collect all additionally-referenced revision IDs.

If parsing fails, ask the user to provide the original diff ID.

### Step 3: Fetch the original revision

```
Tool: phabricator_revision_search
Args: { "constraints": { "ids": [<original_numeric_id>] }, "limit": 1 }
```

Save the original revision's `phid` and note its title and summary.

### Step 4: Get the latest diff ID for each revision

For both the backport and original revisions, get the most recent diff snapshot:

```
Tool: phabricator_diff_search
Args: {
  "constraints": { "revisionPHIDs": ["<revision_phid>"] },
  "order": "newest",
  "limit": 1,
  "attachments": { "commits": true }
}
```

Record the numeric `id` field from the result — this is the diff ID needed for raw diff retrieval.

### Step 5: Get raw diffs

Fetch the raw patch content for both diffs:

```
Tool: phabricator_diff_raw
Args: { "diffID": <diff_id> }
```

Do this for:
- The backport's latest diff
- The original's latest diff
- (Optional) Any additionally-referenced diffs, if their changes need to be verified

### Step 6: Fetch additional referenced diffs (if any)

For each additionally-referenced diff ID found in Step 2:

1. Fetch the revision details via `phabricator_revision_search`
2. Get its latest diff via `phabricator_diff_search`
3. Get its raw diff via `phabricator_diff_raw`

These changes are **expected** to appear in the backport but not in the original.

### Step 7: Compare and review

Compare the backport diff against the original diff. Categorize all differences:

#### 7a. File-level comparison

List files that appear in one diff but not the other.

#### 7b. Hunk-level comparison

For files present in both diffs, compare the actual changes (added/removed lines). Ignore differences in:
- Context line offsets (line numbers shift due to different branch bases)
- Diff metadata headers (`diff --git`, `index` lines, `---`/`+++` paths)

Focus on differences in the actual `+`/`-` lines within hunks.

#### 7c. Classify differences

For each difference found, classify it as:

1. **From an additional referenced diff** — changes that match a diff mentioned in the summary (e.g., D51357). These are expected.
2. **Explained in the commit summary** — the backport summary explicitly describes and justifies the difference (e.g., "removed function X because it doesn't exist on 2024.1", "adapted call to use older API"). These are expected and do not need to be flagged.
3. **Conflict resolution** — changes that look like manual conflict resolution during cherry-pick/backport. Flag for review.
4. **Unexplained difference** — changes that don't match any referenced diff, aren't explained in the summary, and don't look like conflict resolution. Flag as potentially problematic.

### Step 8: Trace unexplained differences back to their origin on master

Skip this step for differences that are already classified as "explained in the commit summary" — those do not need origin tracing.

When the backport contains added code that is NOT in the original diff, NOT from any referenced additional diff, and NOT explained in the commit summary, it was most likely pulled in accidentally during the cherry-pick. This happens when the original commit on master has trailing context (functions defined nearby in the same file) that doesn't exist on the target release branch. Git's conflict resolution during cherry-pick can drag in these unrelated functions.

For each block of unexplained new code:

1. **Identify a distinctive function or symbol name** from the unexplained code (e.g., `YbNewSample`, `yb_maybe_test_fail_ddl`).

2. **Verify it doesn't exist on the target branch**:
   ```
   Tool: phabricator_repository_code_search
   Args: { "repository": "<repo_phid>", "query": "<function_name>", "branch": "<target_branch>" }
   ```
   If it returns empty, the code is not part of the target branch and should not be in the backport.

3. **Confirm it exists on master before the original commit**:
   ```
   Tool: phabricator_repository_code_search
   Args: { "repository": "<repo_phid>", "query": "<function_name>", "commit": "<original_commit_hash>~1" }
   ```
   If found, this confirms the code pre-existed on master and was adjacent to the original change.

4. **Find the commit that introduced it on master** by binary-searching the file history:
   ```
   Tool: phabricator_repository_file_history
   Args: { "path": "<file_path>", "repository": "<repo_phid>", "commit": "<original_commit_hash>~1", "limit": 30 }
   ```
   Then use `phabricator_repository_code_search` at progressively older commits until you find the boundary where the function first appeared. The commit where it appears but its parent doesn't have it is the introducing commit.

5. **Report the origin**: Include the introducing commit hash, its Phorge diff ID, title, and author. Explain that these changes are unrelated to the backport and were pulled in during cherry-pick conflict resolution. They should be removed from the backport.

### Step 9: Produce the review report

Output a structured review with these sections:

**Backport Summary**
- Backport: D<id> — <title>
- Original: D<id> — <title> (commit: <hash>)
- Target branch: <branch from diff refs>
- Additional diffs included: D<id>, D<id>, ... (or "None")

**File Comparison**
- Files in both diffs: list
- Files only in backport: list (with explanation)
- Files only in original: list (with explanation — this might indicate a problem)

**Change Comparison**
- Identical changes: list of files with no meaningful diff differences
- Expected differences (from additional diffs): describe which changes come from which referenced diff
- Differences requiring review: for each difference, quote the relevant `+`/`-` lines from both the original and backport diffs so the reviewer can see exactly what changed. Do not just describe the difference in prose — always include the actual code context.

**Unrelated Code Pulled In From Master** (if any)
- For each block of unexplained code: the file, the functions/symbols involved, the originating commit on master (hash, diff ID, title, author), and confirmation that this code does not exist on the target branch
- Recommendation to remove these from the backport

**Assessment**
- Overall verdict: Clean backport / Minor differences / Needs attention
- Specific concerns, if any

## Notes

- The `phabricator_diff_raw` tool returns the full unified diff. For large diffs, focus comparison on the `+`/`-` lines rather than context.
- Line number offsets in hunks (`@@ -a,b +c,d @@`) will almost always differ between the original and backport — this is normal and expected.
- Some backports are cherry-picks that apply cleanly; others require conflict resolution. Both are valid.
- When the backport summary mentions additional diffs, always fetch and account for those changes before flagging differences.
- Unexplained code in a backport is almost always from unrelated commits on master that were adjacent to the original change in the same file. During cherry-pick, git's conflict resolution can pull in these neighboring functions as context. Always trace such code back to its origin on master rather than just flagging it as "unexplained."
