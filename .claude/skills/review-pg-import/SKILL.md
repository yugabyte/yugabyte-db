---
name: review-pg-import
description: >-
  Review a point-import into an upstream-fork repo such as yugabyte/postgres:
  commits cherry-picked from upstream PostgreSQL onto a yb-pg<version> branch,
  as a GitHub PR, a remote branch, or a local commit range.  Covers cherry-pick
  footer and back-patch-branch checks, mechanical patch-fidelity comparison
  against each upstream commit, verifying conflict-resolution notes instead of
  trusting them, change-set completeness for both CVE batches and ordinary
  imports, and the import-scope policy.  Use when asked to review, verify, or
  check such a PR, branch, or set of cherry-picks.
---

# Reviewing PostgreSQL point-imports

Authoritative process doc:
<https://docs.yugabyte.com/stable/contribute/core-database/merge-with-upstream-repositories>
-- sections "Squash point-imports" and "Review" / "Review cherry-picks".  A
local checkout usually has it at
`docs/content/stable/contribute/core-database/merge-with-upstream-repositories.md`.
Read those sections when the review touches anything this skill does not cover.

Not every import is CVE-driven.  Everything below applies to any point-import;
the parts that differ for security batches are called out.

## 0. Orient before reviewing anything

Establish four things.  Get them wrong and the rest of the review is noise.

```sh
# Resolve remote names -- they differ per checkout.  $YBPG is the yugabyte/postgres
# fork, $PG is true upstream PostgreSQL.  Add whichever the checkout is missing.
YBPG=$(git remote -v | awk '/yugabyte\/postgres/ {print $1; exit}')
PG=$(git remote -v | awk '/postgresql\.org|postgres\/postgres/ {print $1; exit}')
[ -n "$YBPG" ] || { git remote add ybfork https://github.com/yugabyte/postgres; YBPG=ybfork; }
[ -n "$PG" ]   || { git remote add pgupstream https://git.postgresql.org/git/postgresql.git; PG=pgupstream; }

# PR -> local ref
gh pr view <N> --repo yugabyte/postgres --json baseRefName,headRefName,body,commits
git fetch $YBPG pull/<N>/head:pr<N> --force

# What is actually new?
git fetch $YBPG <base_branch>
git merge-base pr<N> $YBPG/<base_branch>
git log --oneline $YBPG/<base_branch>..pr<N>
```

1. **Which `yb-pg<version>` branch is the target**, and therefore which upstream
   `REL_<N>_STABLE` the cherry-picks should come from.  `git ls-remote --heads
   $YBPG` lists them (`yb-pg15`, `yb-pg15-2025.2`, `yb-pg19`, ...).  A branch
   named for a release (`..._2025_2`) targets that release's `yb-pg<version>`
   branch, not the master-line one.
2. **The branch must sit directly on that branch's tip** -- the commit recorded
   in `src/lint/upstream_repositories.csv` in yugabyte-db.  If it is behind, the
   author will hit a csv conflict at land time and has to redo the process.
3. **The exact review scope.**  `git merge-base` against the *wrong* branch
   silently inflates the range.  Confirm the count matches what the PR claims.
4. **Fetch upstream fresh** before any completeness claim:
   `git fetch $PG REL_<N>_STABLE`.  A stale remote will make you report
   missing commits that were never missing, or miss ones that landed yesterday.

## 1. Mechanical per-commit checks

Run these over the whole range at once; they are cheap and they tell you which
commits need actual reading.

```sh
T=$(mktemp -d)   # or the session scratchpad dir
norm()  { git show --format= --no-renames -U3 "$1" | grep -v '^index '; }
norm2() { norm "$1" | sed 's/^@@ -[0-9,]* +[0-9,]* @@/@@/'; }

for c in $(git rev-list --reverse <base>..<head>); do
  x=$(git log -1 --format=%B $c | grep -o '(cherry picked from commit [0-9a-f]*)' \
      | sed 's/.*commit \([0-9a-f]*\))/\1/' | head -1)
  nf=$(git log -1 --format=%B $c | grep -c 'cherry picked from')
  rel=$(git branch -r --contains $x 2>/dev/null | grep -c "$PG/REL_15_STABLE")
  norm  $x > $T/a.diff;  norm  $c > $T/b.diff
  if cmp -s $T/a.diff $T/b.diff; then st=IDENTICAL; else
    norm2 $x > $T/a2.diff; norm2 $c > $T/b2.diff
    cmp -s $T/a2.diff $T/b2.diff && st=OFFSETS-ONLY \
      || st="REAL-DIFF($(diff $T/a2.diff $T/b2.diff | grep -c '^[<>]'))"
  fi
  printf '%-12s footers=%s src=%-13s onREL=%s %-16s %s\n' \
    "${c:0:11}" "$nf" "${x:0:12}" "$rel" "$st" "$(git log -1 --format=%s $c)"
done
```

**Footer.** Every commit needs `(cherry picked from commit <hash>)`.  Two
footers is normal and good: the first is the upstream commit, the second is the
`yb-pg<version>` commit it was re-cherry-picked from (the doc asks for both
hashes when importing YB's own point-imports).  A single footer on a
release-branch import is a nit, not a defect -- but then verify the commit
really is identical to its master-line twin (`§3`).

**Source branch.** For a PG-N import the source should be the
`REL_<N>_STABLE` back-patch, not the master commit -- upstream already resolved
the version conflicts there.  `onREL=0` is not automatically wrong: master-only
commits get imported as prerequisites (e.g. a feature a later security fix
depends on).  It does mean the patch cannot apply verbatim, so expect
substantial documented adaptation.

**Patch fidelity, three tiers:**

- `IDENTICAL` -- done, no reading needed.
- `OFFSETS-ONLY` -- differs only in `@@` line numbers.  Done.  Do not report
  these as differences; they are the normal result of a clean cherry-pick onto
  a tree with different line offsets.
- `REAL-DIFF` -- read it.  Everything in `§2`.

## 2. Reading real differences

```sh
diff -u <(norm2 <upstream>) <(norm2 <cherrypick>)
git show --stat --format= <upstream> ; git show --stat --format= <cherrypick>
```

First compare the **file lists**.  Same files with a different insert/delete
count is the good case: the delta should be fully accounted for by the
documented resolutions.  A missing or extra *file* is a much bigger deal than a
changed line count.

Then classify each hunk in the diff-of-diffs:

- **Context-only** -- the `+`/`-` lines the patch adds are identical, and only
  surrounding context differs because YB carries an extra cherry-pick in that
  region.  Benign, but must still be named in the resolution notes.
- **Genuinely changed added lines** -- the import was adapted.  Must be
  explained by the notes, and the explanation must be *correct* (`§3`).
- **Extra hunks not in the upstream patch** -- usually a hunk borrowed from the
  master version of the same fix, because YB carries a master cherry-pick that
  the back-patch did not have to account for.  Legitimate and common.  Verify
  the borrowed hunk is byte-identical to the master commit's, and that the
  stated reason for needing it is true.
- **Reflowed lines** -- an import that rewrites lines upstream only read as
  context.  Check the resulting file, not the diff: what matters is that the
  final content is semantically right.

Per the doc, also watch for added code landing in the wrong context (expand
with `git show -U50`) and mismatched whitespace or newlines.

## 3. Verify the notes -- do not trust them

This is the highest-value part of the review, and the part a diff read alone
will not do.  Resolution notes are prose written by the author; they assert
facts about both trees.  Check the assertions.

The pattern: for every factual claim in the notes, find the one command that
confirms or refutes it.

| Claim shape | How to check |
|---|---|
| "option code N is free, YB's highest is M" | enumerate every code in the `long_options` array and every `case N:` label; `sort -n \| uniq -d` must be empty |
| "YB has X, upstream doesn't" | `git show <yb-branch>:<file> \| grep -n X` and `git show $PG/REL_N_STABLE:<file> \| grep -c X` |
| "commit ABC introduced this conflict" | `git log -1 --format='%h %s' ABC` and confirm it is in the branch's history |
| "took the hunk from master commit XYZ" | diff the hunk against `git show XYZ -- <file>` |
| "placed it between A and B" | `grep -n` the final file and check the line ordering |
| SGML / structured-file edits | tag balance: `grep -c '<varlistentry'` vs `grep -c '</varlistentry>'`, same for `<term>` |
| "the rest of XYZ is master-only" | spot-check two or three of the listed exclusions actually don't apply |
| struct/array member added at the end | read the final struct, not the diff |

A note that is *correct but incomplete* is also a finding: any real difference
with no corresponding note should be raised.

Silent merges are the dangerous ones.  When two edits sit far apart in a file,
git merges them with no conflict markers and the breakage only shows at test
time.  If a file has both an upstream edit and a known YB cherry-pick, check
the interaction even though git reported no conflict.

## 4. Completeness of the change set

"Is this the full change set?" is the question the mechanical checks cannot
answer.  Approach depends on the kind of import.

### Security batches

Build the coverage table from the branch itself rather than from the PR
description:

```sh
T=${T:-$(mktemp -d)}
git log --format=%B -3000 <head> | grep -o 'cherry picked from commit [0-9a-f]*' \
  | awk '{print $5}' | sort -u > $T/srcs.txt

for cve in CVE-A CVE-B; do
  echo "==== $cve"
  git log --format='%H %ci %s' --grep="$cve" $PG/REL_15_STABLE
done
```

Or sweep every security commit on the branch and mark coverage:

```sh
git log --since=2025-01-01 --format='%H|%ci|%s' $PG/REL_15_STABLE \
| while IFS='|' read h d s; do
    cve=$(git log -1 --format=%B $h | grep -oP '^Security: \K.*'); [ -z "$cve" ] && continue
    grep -q "^$h$" $T/srcs.txt && m="IN " || m=" - "
    printf '%s %s %-16s %s\n' "$m" "${d:0:10}" "$cve" "$s"
  done
```

Notes:

- "Last-minute updates for release notes" and "Update .abi-compliance-history"
  carry every CVE of a release in their trailers.  They are doc/metadata only.
  Filter them out; do not report them as missing.
- A CVE can span several commits (test refactors, prerequisites, a fix split
  across master and back-branches).  Check the count per CVE, not just presence.
- This table is also worth reporting as context: it shows which *other* CVE
  batches the branch is missing, which is usually news to the reader.

### Non-CVE imports

There is no trailer to enumerate, so search structurally:

```sh
# follow-ups referencing the imported commit
git log --format='  %h %ci %s' --grep="<imported_hash_10_chars>" $PG/REL_N_STABLE

# later work in the same files
git log --format='  %h %ci %s' <imported>..$PG/REL_N_STABLE -- <files it touched>

# later work on the same symbol / new API
git log --format='  %h %ci %s' <imported>..$PG/REL_N_STABLE -S'<new_symbol>' -- src/
```

Also compare the touched files against upstream at the import point:

```sh
diff <(git show <upstream_commit>:<file>) <(git show <head>:<file>)
```

If they differ, trace each difference to the upstream commit that caused it
(`git log -S'<line>' $PG/REL_N_STABLE -- <file>`).  Every difference should be
either a deliberate YB delta or an unrelated upstream commit -- never a missing
piece of the import.

Finally, check **prerequisites**: does the import reference a function, GUC, or
header that the branch does not have?  A missing prerequisite usually shows up
as a master-only source commit or as an unexplained adaptation.

## 5. Import scope policy

The standing policy is to import **security fixes only**, not to track upstream
`REL_<N>_STABLE` generally.  Do not recommend importing an ordinary bug fix
just because it is adjacent and looks worthwhile -- there are hundreds, and
choosing among them is a policy question, not a review finding.  Raise it as a
policy question if it seems worth raising, and let the reviewer decide.

The line that *is* in scope: **the security fix, plus any commit that repairs a
regression the security fix introduced.**  To tell the two apart:

- Read the follow-up's message for `Oversight in commit <hash>`.  Resolve that
  hash -- it is often the *master* twin of the back-patch being imported, so
  compare titles, not hashes.
- Check whether the buggy code existed before the import: `git show
  <cve_commit>^:<file>`.  If the defect predates the fix, it is a pre-existing
  upstream bug and out of scope.  If the fix created the code, it is fallout
  and belongs with the import.

Whether a commit is a security fix is decided by evidence, not by subject
matter.  A `Security: CVE-...` trailer is the marker.  Corroborating signals:
security fixes land on release day with no public `Discussion:` link (they are
developed under embargo), and appear in the release-notes commit's CVE list.  A
commit that landed off-cycle with a public `postgr.es` thread is ordinary work,
however security-adjacent it sounds.

## 6. Git metadata

Per the doc's review checklist:

```sh
git log --format='%h | A: %an <%ae> %ai | C: %cn <%ce>' <base>..<head>
```

- Upstream author information must be preserved on every cherry-pick; the
  committer is the person doing the import.
- The branch should contain **only** the cherry-picks.  Anything else needs a
  good reason and a `YB:` title prefix.
- Merge conflicts, including logical ones found via compile or test failure,
  must be resolved and amended into the same commit -- not added as a separate
  fixup commit.
- For the yugabyte-db-side revision: single point-import keeps the upstream
  author; multiple point-imports use the person executing them.

## 7. File-specific advice

- **`doc/`** -- generally does not matter, YugabyteDB does not build it.  The
  exception is `doc/src/sgml/ref`, whose text feeds psql's `\h`.  Check tag
  balance there.
- **Regression tests** -- changes under `src/test/regress`,
  `src/test/isolation`, `src/test/modules`, `contrib/*/expected` etc. need
  `yb.port.` equivalents on the yugabyte-db side.  Check whether the
  corresponding `yb.port.<name>` file exists; if it does not, say so rather
  than assuming either way.  pg_hint_plan is the exception -- YB edits the
  original test directly.
- **Expected-output files can encode buggy behavior.**  A green `make check`
  proves the tree matches its own expected files, not that it is correct.  When
  a fix would change test output, upstream had to update the expected file --
  which means the pre-fix expected file records the bug.  Never cite passing
  tests as evidence that a bug is absent.

## 8. Testing

The author should have run, at each commit:

```sh
./configure && make check
( cd contrib; make check )
( cd src/test/isolation; make check )
( cd src/test/modules; make check )
( cd src/bin/pg_dump; make check )   # needs --enable-tap-tests
```

If the PR has no test plan, say so.  If it has one, take it at face value but
state plainly that you did not re-run it.  Do not imply verification you did
not perform.

## 9. Reporting

Lead with the verdict.  Then, in order: footers/metadata, patch fidelity
(collapse `IDENTICAL` and `OFFSETS-ONLY` into a count -- do not enumerate
non-findings), the real differences and whether the notes justify them, change
-set completeness, and non-blocking notes.

- Separate **already-landed** commits from **new** ones when the branch sits on
  top of earlier imports.  Review both; report the scope split explicitly.
- Show the evidence for a finding -- the blob hashes, the grep count, the
  conflicting line -- not just the conclusion.
- Grade findings honestly.  A cosmetic footer inconsistency and a data
  corruption bug do not belong in the same list.
- Before calling something a blocker, check whether the same defect already
  exists on the branch being imported *from*.  If it does, the import is a
  faithful mirror and the defect is a separate, pre-existing problem -- do not
  hold the train for it.
