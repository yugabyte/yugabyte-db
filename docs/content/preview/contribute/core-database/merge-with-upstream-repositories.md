---
title: How to merge upstream repositories into YugabyteDB
headerTitle: How to merge upstream repositories into YugabyteDB
linkTitle: Merge with upstream repositories
description: How to merge upstream repositories into YugabyteDB
headcontent: How to merge upstream repositories into YugabyteDB
menu:
  preview:
    identifier: merge-with-upstream-repositories
    parent: core-database
    weight: 2914
type: docs
---

[YugabyteDB][repo-yugabyte-db] uses many external codebases.
They are incorporated in one of the following ways:

- Inside the [thirdparty repository][repo-thirdparty].
- Embedded into some subdirectory in the [yugabyte-db repository][repo-yugabyte-db].
- Downloaded on the fly as part of build or test (e.g. YB Controller).
- [Prerequisite packages or binaries](./build-from-src-almalinux.md).

This document is primarily concerned about the case of code being embedded into the [yugabyte-db repository][repo-yugabyte-db].

## Different tracking approaches

In the beginning, there was no formality importing embedding external code.
Examples include RocksDB and Kudu.
Such code is now so closely entwined with YugabyteDB's modifications such that it is difficult to update them to newer versions.

PostgreSQL also technically fell under that bucket.
However, today, it is in a better state with formality imposed onto it.
That formality primarily comes from [`upstream_repositories.csv`][upstream-repositories-csv].

### How upstream repositories are tracked

[`upstream_repositories.csv`][upstream-repositories-csv] specifies which commit is being tracked in each upstream repository.
(Naturally, this means that it will not work for external code that is not in the form of a git repository, but in this day and age, that is not likely to happen.)
This makes it apparent what changes YugabyteDB made against the upstream repository.

In case the commit being tracked is present in the external repository, this may seem trivial to do anyway without [this CSV][upstream-repositories-csv].
However, YugabyteDB occasionally takes point-imports of external repository commits, such as security fixes.
To make matters worse, those point-imports can have conflicts on the external repository side, and they could have other conflicts on the [yugabyte-db repository][repo-yugabyte-db] side as well.
Do that ten or so times, and it becomes difficult to track which changes are made by YugabyteDB and which changes are point-imported.
The CSV necessitates a single commit tracking what upstream state is being tracked.
Therefore, in case of port-imports, YugabyteDB must create a fork of the external repository to track the state of the upstream code with the point-imports.

To make matters more complicated, the upstream repository might be tracked differently across [`yugabyte/yugabyte-db`][repo-yugabyte-db] branches.
For example, pgaudit has different branches for each PG version while pg_cron has a single branch where, at any point, there is multi-version support.
In case of the former, it is necessary to track separate upstream commits across [`yugabyte/yugabyte-db`][repo-yugabyte-db] branches, and if a fork is required, then a branch should be created for each PG version that YugabyteDB supports, conventionally named `yb-pg<version>`.
In case of the latter, one may do the same, or a single `yb` branch can be used across all [`yugabyte/yugabyte-db`][repo-yugabyte-db] branches.
Branches can be renamed, so if the single `yb` branch ever needs to be split to two, it can switch to the `yb-pg<version>` format from that point.
It's fine as long as historical commits are not lost so that old commits still have a valid upstream commit that can be referenced.

Not all external repositories are yet tracked in [`upstream_repositories.csv`][upstream-repositories-csv].
New repositories ought to be registered in here.
Old repositories should be migrated into the CSV over time.
Today, all repositories under `src/postgres` are tracked in the CSV.
By using the CSV, several linter rules have been made to ensure YugabyteDB stays aligned with the upstream repositories.
Note: as of today, there is no foolproof linter check that the CSV is updated properly whenever a merge with an upstream repository is made.

### How upstream repositories are embedded

Orthogonally, there is the matter of how external repositories are embedded into YugabyteDB.
Historically, this has been via squash merge, committing everything as one large commit.
There had also been usage of git submodules in the past, but that was scrapped primarily because developers accidentally reverted the submodules to older commits.
That is not a strong reason considering checks could be added to counter that; however, submodules often do not make much sense when they do not compile standalone because of strong dependencies with YugabyteDB itself.

So a second strategy for embedding external repositories was introduced, namely git subtrees.
Several external repositories have been converted into this format, and some new repositories have been added using this format.
However, YugabyteDB has not yet taken a strong stance on whether this strategy should be adopted.
See the pros and cons in the following section.

## Git subtrees

### Pros and cons

Pros of using git subtrees over squash merge:

- Get all upstream commit history.
  This makes git blame lookup more convenient.
- Direct merge commits.
  Not only is performing the merge easier and less prone to error, but the history it leaves behind is clearer and easier to review.

Cons of using git subtrees over squash merge:

- Get all upstream commit history.
  This is listed as both a pro and con, the con being it can bloat the `yugabyte/yugabyte-db` repository.
- More complex process which involves more steps.
- `git subtree` is not built into `git` by default.
- Git commands using paths do not work well with subtrees.
  For example, with the squash merge embedding strategy, `git log -- src/postgres/third-party-extensions/postgresql-hll` lists all commits touching that path.
  However, with the subtree embedding strategy, `git log -- src/postgres/third-party-extensions/documentdb` does not list such commits.
  And instead, `git log -- documentdb_errors.csv` lists those commits.
  Which is to say, the paths of all these upstream repositories are mapped to the same base.
  So common paths such as `git log -- .gitignore` show a mix of all `.gitignore` changes across the upstream repositories using subtrees.

There is currently no strong preference for one over the other, though currently, upstream repositories have only migrated from squash merge to subtree, never the other way around.

### Adding a new upstream repository as subtree

When trying to introduce a new upstream repository using the subtree embedding strategy, it is pretty straightforward.

The following steps details what has already happened with adding DocumentDB.

1. Find an appropriate upstream repository commit to target.
   Normally, this would correspond to a Git tag as an official release for stability.
1. Fetch that tag to `yugabyte/yugabyte-db`.

   ```sh
   git remote add documentdb https://github.com/microsoft/documentdb.git
   git fetch documentdb tags/v0.102-0:tags/v0.102-0 --no-tags
   ```

   In case of a conflict with existing tags, there are many workarounds, one of which is to delete the existing tag first then re-run the fetch.
1. Find an appropriate location to embed the repository.
   The common case is PostgreSQL extensions, which belong in `src/postgres/third-party-extensions/<extension_name>`.
1. Add the subtree.
   You may want to switch to a new feature branch first, based off the latest `yugabyte/yugabyte-db` `master` branch.

   ```sh
   git fetch origin master:master
   git switch -c add-documentdb-subtree master
   git subtree add --prefix=src/postgres/third-party-extensions/documentdb documentdb v0.102-0 --no-squash
   ```

   If `git subtree` does not work, you may need to install it.
   There may be an alternative method to do this using `git merge -s subtree`, but that has not been tested as of yet.
   In any case, `git subtree` adds some useful metadata to the commit message, so using that is preferred.

   There should be no merge conflicts since this is a brand new upstream repository in a brand new directory.
   Any modifications to integrate this upstream repository to YugabyteDB should be done as a followup after this whole process.
   In this example, this was done by commit [f8e9b50f83a4561b1314057297e3ba3236c7703c](https://github.com/yugabyte/yugabyte-db/commit/f8e9b50f83a4561b1314057297e3ba3236c7703c).
1. Add the upstream repository details into `src/lint/upstream_repositories.csv`.
   Commit this single change as one commit on top of the subtree merge commit.
   In this example, this was done by commit [3201d6f146c957b39ce23f3c59e76ca31ffa1a0c](https://github.com/yugabyte/yugabyte-db/commit/3201d6f146c957b39ce23f3c59e76ca31ffa1a0c).
1. Submit this feature branch as reference for review.
   Phorge is not sufficient since it strips Git metadata.
   One example of a place to submit this is a personal GitHub fork of `yugabyte/yugabyte-db`.
   The reviewer should make sure the subtree merge is done properly and that exactly one commit is added after that concerning `src/lint/upstream_repositories.csv`.
1. Create a Phorge revision of this feature branch for official review.
   Make sure lint is not skipped to make sure `src/lint/upstream_repositories.csv` is valid.
   The reviewer should make sure the latest commit hash of both submissions are exactly equal.
1. Request permission from @buildteam in the internal slack #eng-infra channel to land a merge commit of this revision.
1. Land the change using the merge strategy:

   ```sh
   arc land --strategy merge --revision D...... --onto master
   ```

   Append `--hold` if it is your first time and you are afraid of making mistakes.
   After [checking the structure looks good](#subtree-illustrations) using a command such as `git log --oneline --graph`, push the change using the sample command provided at the end of the `arc land --hold` output.
   In this example, this was done by commit [b01e29bafa6fb8bfb899f0b3ac6e98363340c8e7](https://github.com/yugabyte/yugabyte-db/commit/b01e29bafa6fb8bfb899f0b3ac6e98363340c8e7).

### Converting a squash embedded upstream repository to subtree

Working with an existing squash embedded repositories to the subtree strategy is a lot more complicated.
This is primarily because of the hoops one has to jump through to make `git blame` point to the subtree rather than the old squash commits.

You might be trying to update the upstream repository at the same time.
In that case, it is strongly recommended to first convert the repository to the subtree embedding strategy at the existing version, then [update it to the new version](#updating-a-subtree).
This is less prone to mistakes and easier to review.

The following steps details what has already happened with updating pgaudit.
Note that this was done on the `pg15` branch at the time rather than `master`.

1. Subtree merge the existing version.

   ```sh
   git remote add pgaudit https://github.com/pgaudit/pgaudit
   git fetch pgaudit tags/v1.3.2:tags/v1.3.2 --no-tags

   git fetch origin pg15:pg15
   git switch -c convert-pgaudit-to-subtree pg15
   git merge -s subtree -Xsubtree=src/postgres/third-party-extensions/pgaudit 1.3.2 --allow-unrelated-histories
   git commit-tree -p 'HEAD^2' -p 'HEAD^1' 'HEAD^{tree}'
   ```

   The merge might have conflicts.
   Make sure to resolve such conflicts and detail them in the commit message.

   Note the last command swaps the left and right parents so that the subtree's commits are prioritized.
   This does not work perfectly, especially when files are unchanged by this re-merge.
   It is still better to try, though.

   All conflicts should be resolved, and build should work after this single subtree merge commit.
   In this example, that commit is [329515ec6111daf5fe1f7d169de3a437273ad2c3](https://github.com/yugabyte/yugabyte-db/commit/329515ec6111daf5fe1f7d169de3a437273ad2c3), and build did not necessarily work since this was during the unstable situation of PG 15 merge.
   At least the example commit message can be used as reference.
1. At this point, if you plan to update to a newer version, there's nothing stopping you from directly doing that next.
   However, in this example, it was done step by step.
   The rest of the steps in this section are related if you want to do it step-by-step.
   Otherwise, skip to the next section.
1. The remaining steps are equivalent to [before](#adding-a-new-upstream-repository-as-subtree), starting from adding repository details to `src/lint/upstream_repositories.csv`.
   There is no example commit for the `upstream_repositories.csv` change since it did not exist at the time.
   There is an example commit for the Phorge revision that was merge landed: [2349d7c2df5a519677b80a8eae902a816f34f95b](https://github.com/yugabyte/yugabyte-db/commit/2349d7c2df5a519677b80a8eae902a816f34f95b).

For updating to the new version, the steps follow in the next section.

### Updating a subtree

If the repository was embedded as subtree from the very beginning, this again becomes easier.
Otherwise, some merge commit parent swapping is suggested to continually improve the number of files that swap to using the subtree for `git blame`.

1. Subtree merge the new version.

   ```sh
   git remote add pgaudit https://github.com/pgaudit/pgaudit
   git fetch pgaudit tags/v1.7.0:tags/v1.7.0 --no-tags

   git fetch origin pg15:pg15
   git switch -c convert-pgaudit-to-subtree pg15
   git merge -s subtree -Xsubtree=src/postgres/third-party-extensions/pgaudit 1.7.0
   git commit-tree -p 'HEAD^2' -p 'HEAD^1' 'HEAD^{tree}'
   ```

   If directly following up from a previous subtree merge, skip the `git switch` step.

   Notice that, unlike [converting to a subtree for the first time](#converting-a-squash-embedded-upstream-repository-to-subtree), this merge does not need `--allow-unrelated-histories`.

   In case of subtrees that were created as such from the very beginning, the `git commit-tree` should be omitted since there are likely no `git blame` issues.
   Also, there is likely a cleaner way to do the merge using `git subtree` command(s) directly.
   Experimenting with that can be left as a followup.

   All conflicts should be resolved, and build should work after this single subtree merge commit.
   In this example, that commit is [180a1f7613457bc84021f3c8c186adadc92ba626](https://github.com/yugabyte/yugabyte-db/commit/180a1f7613457bc84021f3c8c186adadc92ba626).
1. The remaining steps are equivalent to [before](#adding-a-new-upstream-repository-as-subtree), starting from adding repository details to `src/lint/upstream_repositories.csv`.
   There is no example commit for the `upstream_repositories.csv` change since it did not exist at the time.
   There is an example commit for the Phorge revision that was merge landed: [dd24929b2c26f282221fe4e31d2b93ce60f5dfba](https://github.com/yugabyte/yugabyte-db/commit/dd24929b2c26f282221fe4e31d2b93ce60f5dfba).

### Subtree illustrations

The example of documentdb [subtree addition](#adding-a-new-upstream-repository-as-subtree) can be visualized with `git log --oneline --graph b01e29bafa6fb8bfb899f0b3ac6e98363340c8e7`:

```
*   b01e29bafa [#26749] OpenDocDB: Import documentdb extension v0.102-0
|\
| * 3201d6f146 Update upstream_repositories with documentdb extension
| *   f8e9b50f83 Add 'src/postgres/third-party-extensions/documentdb/' from commit 'f31e0b6cadda66c3cc7f7087365e996c212cfffe'
| |\
| | * f31e0b6cad Merged PR 1615832: Support for pushdown of in to PFE indexes
| | * 80df5a1509 Merged PR 1604750: [perf][creation_time] Alter creation time : part 1
| | * 0c77472a67 Merged PR 1614124: [Infra] adding support for documentdb_distributed extension in start_oss_server
| | * 5a8db3494c Merged PR 1614043: [Operator] Support extended $getfield for 8.0
| | * 86ce366053 Merged PR 1556142: DateFromString part-2: Add more functionalities to $dateFromString and make JS tests pass
| | * 399e01d954 Merged PR 1539417: [Operator] $toUUID in Mongo 8.0
| | * 3a369bf6a3 Fix handling of explicit `maxTimeMS` zero values (#41) (#111)
...
| | * 03c69f703d Merged PR 1249283: [Index Truncation] Add more tests and allow per index registration of truncation
| | * 628c576d6f Merged PR 1250361: Modify more references for OSS
| | * d6b2c3aab4 Merged PR 1248408: [OSS] Fix listIndexes and listCollections command
| | * 9839f0610e Merged PR 1249793: Changing extension name to helio in all OSS files
| | * e56a5f6edb Merged PR 1216978: Add Index details in CurrentOp for CREATE INDEX
| | * cd6050eb08 Merged PR 1245028: [pgmongo][Geospatial] - enable Geospatial for Tests
| | * ffa180741a Merged PR 1205625: Migrate drop_collection to C
| | * 4c6b6d1fde [Infra] Add devcontainer for build time setup to HelioDB (#4)
| | *   4aea8125e4 Merge pull request #3 from microsoft/visridha/intial_helio_commit
| | |\
| | | * d8671eb379 Initial commit for HelioDB
| | |/
| | *   9876da1a14 Merge pull request #1 from microsoft/users/GitHubPolicyService/6beb23d4-53ad-46b8-bce4-dd7d94d42d34
| | |\
| | | * 89d98ead6d Microsoft mandatory file
| | * | 54147c75a4 SECURITY.md committed
| | * | fd4f59322c README.md updated to template
| | * | cded62f618 SUPPORT.md committed
| | * | 0962f9119c LICENSE updated to template
| | * | cfc3e2086b CODE_OF_CONDUCT.md committed
| | |/
| | * cc150f8608 README.md: Setup instructions
| | * c4234ea994 Initial commit
* | 7ae4c354b8 [PLAT-17251]: Fix Backup deletion metrics labels
* | 3c9922199c [PLAT-17214] YB Instance Tags are not saved during "Resize Node changes"
* | 03d9aaf29f [#25710] YSQL: Fix path->rows set by yb_cost_bitmap_table_scan
* | 2132441a27 [#26632] YSQL: Skip ModulesDummySeclabel test with conn mgr
* | 3d73cd1a6b [PLAT-17261] Fix date conversion bug in get jwt endpoint
* | a665c196d9 (tag: 2.25.2.0-b301) [#26680] Docdb: Table Locks : Improve debuggability/logging
* | f80c0b0621 (tag: 2.25.2.0-b300) [PLAT-17139] Change the default node_agent log to UTC
* | e99df6f4d9 [#26746] YSQL: merge PG 15.12
* | 558b6cb51b [doc] Standardize icons (#26747)
|/
* bc7ef0969c (tag: 2.25.2.0-b299) [PLAT-17222]  Revamped check cluster consistency to handle dual nics
```

The example of pgaudit [subtree conversion](#converting-a-squash-embedded-upstream-repository-to-subtree) and [subtree update](#updating-a-subtree) can be visualized with `git log --oneline --graph dd24929b2c26f282221fe4e31d2b93ce60f5dfba`:

```
*   dd24929b2c [pg15] merge: pgaudit tag '1.7.0' into pg15
|\
| *   180a1f7613 Merge pg15 into pgaudit tag '1.7.0'
| |\
| * | 8349710fbb Add caveat about auditing superusers.
| * | ee1c3f5d04 PostgreSQL 15 support.
| * | 1930790e4b Documentation updates missed in PostgreSQL 14 release.
| * | 02d3dfd91b Add explanation why `CREATE EXTENSION` is required.
| * | 6a3ab20747 Explicitly grant permissions on public schema in expect script.
| * | 959f0652ea Reorder container scripts for more efficient builds across versions.
| * | 605aa9dad1 Fix typo in pgaudit.role help.
| * | 267eb83a14 Stamp 1.6.2.
| * | 6460d9fec7 Skip logging script statements for create/alter extension.
| * | 52d3ff4f13 Update copyright end year.
| * | 881c617084 Add security definer and search_path to event trigger functions.
| * | 4c3a5023f8 Guard against search-path based attacks.
| * | 6afeae52d8 Remove remaining references to Vagrant.
| * | bd6a261f72 Fix logic to properly classify SELECT FOR UPDATE as SELECT.
| * | bb816445df Add RHEL test container.
| * | e8cded51a4 Add pgaudit.log_rows setting.
| * | ed6975c522 Add container remove to test command.
| * | 70b30d4379 Update for ProcessUtility_hook_type changes for 14beta2.
| * | 002f2c3c3b PostgreSQL 14 support.
| * | e3d79b03ee Run make clean for each test.
| * | 6b56031e87 Remove Vagrantfile.
| * | e2e5a69c4d Add automated testing using Github Actions.
| * | c6d958bb4d Revert "PostgreSQL 14 support."
| * | b045fb9b90 PostgreSQL 14 support.
| * | 5b0a3a6c1b Add .editorconfig.
| * | 8831cef691 Add pgaudit.log_statement setting.
| * | 28faa197d3 Update copyright end year.
| * | 94a2ae8c20 Remove PostgreSQL 13 repository used for pre-release testing.
| * | fd4319f7c8 Improve compile and install instructions.
| * | 7169e84e1a Remove make check from compile and install section of README.md
| * | 5096e75f1a Update version in README.md to PostgreSQL 13.
| * | 33248d2222 Suppress logging for internally generated foreign-key queries.
| * | 437a537345 Fix "pgaudit stack is not empty" error.
| * | c07aa8254d Fix misclassification of partitioned tables/indexes.
| * | 2fcf4f5460 Update copyright end year.
| * | 7053d0a0f3 Use syscache to get relation namespace/name.
| * | 387db257f1 Update to PostgreSQL 13.
| * | e1b2d890a3 Update version, documentation, and tests for PostgreSQL 12.
| * | 8c76e69de9 Update master to PostgreSQL 12.
| * | dff82bc137 Update copyright end year.
| * | 778d9efb35 Update Vagrantfile with new box version and PostgreSQL repository.
| * | b1d81db598 Add new logging class MISC_SET.
| * | 4dab520da7 Add SET to documented list of commands in the MISC class.
| * | 142f4e9460 Document that <none> or <not logged> may be logged in audit entries.
| * | 94cad8c8cb Add [%p] to suggested log_line_prefix.
| * | 9fd8f04b22 Remove extraneous escapes in log prefix example.
| * | 330c2177c8 Add ALL to the list of logging classes.
| * | 1f65fe9c98 Fix DO example syntax.
| * | b2040b6e6c Deep copy queryDesc->params into the audit stack.
* | | a0c0bda6b3 [pg15] test: fix shell tests failing on query id
| |/
|/|
* |   2349d7c2df [pg15] merge: pgaudit tag '1.3.2' into pg15
|\ \
| * \   329515ec61 Merge pg15 into pgaudit tag '1.3.2'
| |\ \
| |/ /
|/| |
* | | a4cd9c7136 [pg15] test: fix output order issue in yb_pg15
```

### Historical attempts using subtree split

When first evaluating different embedding strategies, we decided to go with a manual subtree approach.
This works by adding a commit that manually moves all of the upstream repository's files under the new prefix.
That preserves upstream commit hashes and is a lot simpler to reason about.

But after some time, we discovered that it causes excessive merge conflicts, for example on the `.gitignore` between YugabyteDB and the upstream repository.
So these changes were forcibly erased from the repository history and replaced with real subtrees.

At that point in time, the subtree conversion of squash merged repositories was done using `git subtree split`, which duplicates some YugabyteDB commits.
Only later was it discovered that there is a way to do the conversion without creating those by duplicate commits using `git merge -s subtree`.
Unlike before, this time, the old changes were not erased from the repository history and still remain today.
The latest such change is commit [700f5fb13993201d1ac0652d731a8bfc2eb86331](https://github.com/yugabyte/yugabyte-db/commit/700f5fb13993201d1ac0652d731a8bfc2eb86331).

## Types of merges

There are three main ways upstream code is updated then merged into YugabyteDB.

- Point-import one or a few commits.
- Merge to a direct-descendant commit (e.g. minor version upgrade).
- Merge to a non-direct-descendant commit (e.g. major version upgrade).

In addition, there are slight differences depending on how the upstream repository is embedded.

### Embedded via squash

The steps that follow will use PostgreSQL as an example.
At the time of writing, PostgreSQL uses the squash embedding strategy.

#### Squash point-imports

1. Find the appropriate upstream postgres commits.
   For example, if this import is for `yugabyte/yugabyte-db` repo `2025.1` branch based on PG 15.x, then prioritize using upstream postgres commits on `REL_15_STABLE` over those on `master` because they would have resolved conflicts for us.
   See [here](#find-postgresql-back-patch-commits) for suggestions on how to do this.
1. Switch to a feature branch off the latest `yugabyte/postgres` repo `yb-pg<version>` branch: `git switch -c import-abc yb-pg<version>`.
1. Do `git cherry-pick -x <commit>` for each commit being imported.
   Notice the `-x` to record the commit hash being cherry-picked.
   [Make sure tests pass](#testing-postgresql).
   For any merge conflicts, resolve and amend them to that same commit, describing resolutions within the commit messages themselves.
   This includes logical merge conflicts which can be found via compilation failure or test failure.
   At the end, you should be n commits ahead of `yb-pg<version>`, where n is the number of commits you are point-importing.
   Make a GitHub PR of this through your fork for review.
1. On the `yugabyte/yugabyte-db` repo, import the commits that are part of the `yugabyte/postgres` repo PR.
   This is not as straightforward as a cherry-pick since it is across different repositories: see [cross-repository patch](#cross-repository-patch) for advice.
   Unlike as in `yugabyte/postgres`, the commit structure in `yugabyte/yugabyte-db` does not matter since these changes will eventually be squashed.
   Once all commits are imported, update `src/lint/upstream_repositories.csv`, and create a Phorge revision.
   The Phorge summary should have the upstream postgres commit hashes and their commit messages.
   Add to the summary the merge resolution details that were previously recorded.
1. Pass review for both the `yugabyte/postgres` imports and the `yugabyte/yugabyte-db` revision.
   Besides general merge review, here are things reviewers should watch out for in the `yugabyte/postgres` review:

   - Was `-x` used for each cherry-pick?
   - Was the [commit author metadata](#git-author-information) preserved for each cherry-pick?
   - Was the commit message preserved for each cherry-pick?
   - Are there exactly n commits, and are they branched off the latest `yb-pg<version>` commit?

1. Land the `yugabyte/yugabyte-db` revision.
   **If there is a merge conflict on `src/lint/upstream_repositories.csv`, then redo the whole process.**
   It means someone else updated `yb-pg<version>` branch, so `yugabyte/postgres` cherry-picks need to be rebased on latest `yb-pg<version>`, compilation/testing needs to be re-done, and since the commit hashes change, `src/lint/upstream_repositories.csv` needs a new commit hash.
   Plus, it's safest to re-run tests on `yugabyte/yugabyte-db` after all this adjustment.
1. Push the `yugabyte/postgres` commits directly onto `yb-pg<version>`: `git push <remote> import-abc:yb-pg<version>`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv`, and if landing that change passes, then no one should have touched `yb-pg<version>` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg<version>`.**
1. If backporting to stable branches, the same process should be repeated using the `yb-pg<version>` branch corresponding to that stable branch's PG version.
   The [search for back-patch commits](#find-postgresql-back-patch-commits) should be redone for each PG version.

#### Squash direct-descendant merge

A direct-descendant merge for PostgreSQL is typically a minor version upgrade.
An example is PG 15.2 to 15.12 as done in [`yugabyte/postgres` 12398eddbd531080239c350528da38268ac0fa0e](https://github.com/yugabyte/postgres/commit/12398eddbd531080239c350528da38268ac0fa0e) and [`yugabyte/yugabyte-db` e99df6f4d97e5c002d3c4b89c74a778ad0ac0932](https://github.com/yugabyte/yugabyte-db/commit/e99df6f4d97e5c002d3c4b89c74a778ad0ac0932).

1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb-pg15` branch since a `postgres/postgres` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut could not be used.)
1. Otherwise, switch to a feature branch off the latest `yugabyte/postgres` repo `yb-pg<version>` branch: `git switch -c merge-pg yb-pg<version>`.
   Then, do `git merge REL_15_12`.
   (It may still be the case that the target version, in this example 15.12, already has contains all the changes YB imported on top of 15.2.
   If that is the case, then the merge can be simplified to take exactly the target version: `git merge -X theirs REL_15_12`.
   In this example, that was not possible since YB imported some newer commits.)
   [Make sure tests pass](#testing-postgresql).
   Record merge resolutions into the merge commit message.
   There should only be the merge commit and no other extraneous commits on top of it.
   Make a GitHub PR of this through your fork for review.
1. Apply those changes to the `yugabyte/yugabyte-db` repo.
   See [cross-repository patch](#cross-repository-patch) for details.
   Update `src/lint/upstream_repositories.csv`, and create a Phorge revision listing all the merge conflict details.
   Ensure that the [author information](#git-author-information) for the latest commit of this Phorge revision is `YugaBot <yugabot@users.noreply.github.com>`.
1. Pass review for both `yugabyte/postgres` GitHub PR and `yugabyte/yugabyte-db` Phorge revision.
   If `yugabyte/postgres` needs changes, then commit hashes change, so `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv` must be updated, and re-test on `yugabyte/yugabyte-db` should be considered.
1. Land the `yugabyte/yugabyte-db` revision.
   **If there is a merge conflict on `src/lint/upstream_repositories.csv`, then redo the whole process.**
   It means someone else updated `yb-pg15` branch, so `yugabyte/postgres` merge need to be redone on latest `yb-pg15`, compilation/testing needs to be re-done, and since the commit hashes change, `src/lint/upstream_repositories.csv` needs a new commit hash.
   Plus, it's safest to re-run tests on `yugabyte/yugabyte-db` after all this adjustment.
1. Push the `yugabyte/postgres` merge directly onto `yb-pg15`: `git push <remote> merge-pg:yb-pg15`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv`, and if landing that change passes, then no one should have touched `yb-pg15` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg15`.**

#### Squash non-direct-descendant merge

A non-direct-descendant merge for PostgreSQL is typically a major version upgrade.
An example is PG 11.2 to 15.2 as _initially_ done in [`yugabyte/yugabyte-db` 55782d561e55ef972f2470a4ae887dd791bb4a97](https://github.com/yugabyte/yugabyte-db/commit/55782d561e55ef972f2470a4ae887dd791bb4a97).
Note that this was done before `upstream_repositories.csv` was in place, so it lacks formality and should not be looked at as a model example.

This merge can technically be handled similarly as [direct-descendant merge](#squash-direct-descendant-merge).
However, there is a much faster alternative that should be taken.
A prerequisite for using this alternative is that the target version contains all the commits of the source version (or, nearly equivalently, the target version was released later than the source version).
For example, PG 15.2 should contain all commits in PG 11.2, considering backports as equivalent to each other, and for any commits only in PG 11.2, they are likely irrelevant for PG 15.2.
Another example, PG 15.12 to PG 16.1 cannot use this alternative strategy since 15.12 has backports from master that 16.1 does not have.
This prerequisite generally shouldn't fail since major version upgrades should aim for the latest minor version of a major version.

The steps below detail a hypothetical merge of PG 15.12 to PG 18.1.
At the time of writing, YugabyteDB is based off PG 15.12.

1. Ensure PG 15.12 to PG 18.1 satisfies the prerequisite mentioned above.
1. On the `yugabyte/postgres` repo, identify the point-imports or custom changes on `yb-pg15` that need to be carried over to PG 18.1.
   The total list of point-imports and custom changes can be found using `git log --first-parent --oneline yb-pg15`, ignoring merge commits and excluding anything before the first target minor version for this major version, which is REL_15_2.

   - For each point-import commit, determine whether it is already contained in the target version, `REL_18_1`.
     See [find PostgreSQL back-patch commits](#find-postgresql-back-patch-commits).
     If it is contained, drop them from consideration: they are not needed.
     If it is not contained, find the appropriate commit for the target version: if there is an equivalent back-patch commit in `REL_18_STABLE`, that is preferable; use `master` otherwise.
     Record that commit for later use.
   - Sometimes, there are changes that are not point-imports.
     Manually check whether those changes exist in the target version on a case-by-case basis.
     Record those changes for later use.
1. Create a new branch `yb-pg18` on `REL_18_1`, then apply each change recorded above.
   If cherry-picking commits, use `-x`.
   For merge conflicts, resolve and amend them to the same commit, describing resolutions within the commit messages themselves.
   The procedure here is very much like the first three steps of [doing point-imports](#squash-point-imports).
1. Do the same steps as in [direct-descendant merge](#squash-direct-descendant-merge), starting from "Apply those changes to the `yugabyte/yugabyte-db` repo."
   A difference is that this merge may be so much larger that the initial merge is incomplete and potentially based off an old commit on `yugabyte/yugabyte-db`.
   This initial merge commit and further development to close the gaps can be done in a separate `pg18` branch on `yugabyte/yugabyte-db`.
   This is what was done for the PG 15 merge, from `yugabyte/yugabyte-db` [55782d561e55ef972f2470a4ae887dd791bb4a97](https://github.com/yugabyte/yugabyte-db/commit/55782d561e55ef972f2470a4ae887dd791bb4a97) to [eac5ed5d186b492702a0b546bf82ed162da506b0]((https://github.com/yugabyte/yugabyte-db/commit/eac5ed5d186b492702a0b546bf82ed162da506b0).

### Embedded via subtree

#### Subtree point-imports

1. If this subtree is currently not forked by YugabyteDB, ask yourself whether this point-import is necessary.
   If you instead merge to a newer version, you avoid the need to make a YugabyteDB fork.
1. If there is no YugabyteDB fork, that needs to be created and [a new branch added to it](#how-upstream-repositories-are-tracked).
   Using pgaudit as an example, `git switch -c yb-pg15 "$(grep pgaudit src/lint/upstream_repositories.csv | cut -d, -f4)"`.
1. Do `git cherry-pick -x <commit>` for each commit being imported.
   Notice the `-x` to record the commit hash being cherry-picked.
   Do any relevant testing if applicable.
   For any merge conflicts, resolve and amend them to that same commit, describing resolutions within the commit messages themselves.
   This includes logical merge conflicts which can be found via compilation failure or test failure.
   At the end, you should be n commits ahead of the commit in `upstream_repositories.csv`, where n is the number of commits you are point-importing.
   Make a GitHub PR of this through your fork for review.
1. On the `yugabyte/yugabyte-db` repo, [subtree merge](#git-subtrees) this branch.

#### Subtree direct-descendant merge

The steps that follow will use documentdb as a hypothetical example.
Suppose that YugabyteDB was based off a `yugabyte/documentdb` repo `yb` branch constructed from `v0.102-0` with commit [b896737f53d9eb13e0b397976e1b2edd310cca57](https://github.com/microsoft/documentdb/commit/b896737f53d9eb13e0b397976e1b2edd310cca57) cherry-picked.
We are trying to merge to `v0.103-0` (which doesn't exist at the time of writing, but you get the idea of the example).

1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb` branch since a `microsoft/documentdb` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut can not be used.)
1. Otherwise, switch to a feature branch off the latest `yugabyte/documentdb` repo `yb` branch: `git switch -c merge-documentdb yb`.
   Then, do `git merge v0.103-0`.
   (It may still be the case that the target version, in this example `v0.103-0`, already has contains all the changes YB imported on top of `v0.102-0`.
   If that is the case, then the merge can be simplified to take exactly the target version: `git merge -X theirs v0.103-0`.)
   Do any relevant testing if applicable.
   Record merge resolutions into the merge commit message.
   There should only be the merge commit and no other extraneous commits on top of it.
   Make a GitHub PR of this through your fork for review.
1. On the `yugabyte/yugabyte-db` repo, [subtree merge](#git-subtrees) this branch.

#### Subtree non-direct-descendant merge

The steps that follow will use pgaudit as an example.

This merge can technically be handled similarly as [direct-descendant merge](#squash-direct-descendant-merge).
However, there is a much faster alternative that should be taken.
A prerequisite for using this alternative is that the target version contains all the commits of the source version (or, nearly equivalently, the target version was released later than the source version).
For example, `1.7.0` should contain all commits in `1.3.2`, considering backports as equivalent to each other, and for any commits only in PG `1.3.2`, they are likely irrelevant for PG `1.7.0`.
This prerequisite generally shouldn't fail since major version upgrades should aim for the latest minor version of a major version.

Suppose that YugabyteDB was based off a `yugabyte/pgaudit` repo `yb-pg11` branch constructed from `1.3.2` with commit [455cde5ec3a4374b18ad551aaabe6d60761b6503](https://github.com/pgaudit/pgaudit/commit/455cde5ec3a4374b18ad551aaabe6d60761b6503) cherry-picked.
We are trying to merge to `1.7.0`.

1. Ensure `1.3.2` to `1.7.0` satisfies the prerequisite mentioned above.
1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb-pg11` branch since a `pgaudit/pgaudit` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut can not be used.)
1. Otherwise, follow the steps to identify commits to cherry-pick and create a `yb-pg15` branch as in [squash non-direct-descendant merge](#squash-non-direct-descendant-merge).
1. On the `yugabyte/yugabyte-db` repo, [subtree merge](#git-subtrees) this branch.

## General advice

MERGE:
- port regress tests

### Git author information

Each commit has the following six pieces of metadata.
(See `git commit --help` "COMMIT INFORMATION" section for more details.)

- GIT_AUTHOR_NAME
- GIT_AUTHOR_EMAIL
- GIT_AUTHOR_DATE
- GIT_COMMITTER_NAME
- GIT_COMMITTER_EMAIL
- GIT_COMMITTER_DATE

Normally, `git cherry-pick`ing a commit preserves that commit's author information.
Otherwise, the author information is generated with your configuration.
`git commit --amend --reset-author` also regenerates the author information with your configuration.
Take care not to overwrite the author information when you shouldn't.

For Phorge, if the squash strategy is used for landing, the author information of the latest commit being landed is transferred to the squash commit.

### Cross-repository patch

Cherry-picking a change from one repository to another where the prefixes of paths differ is a little tricky.
It is most commonly done for point-imports of upstream repositories to YugabyteDB.

If you are new to it, you may be tempted to copy-paste code changes from one side to the other.
Please do not do that as it is error-prone.

A smarter way to do it is to get a patch of the upstream changes, prefix all paths to the appropriate location in `yugabyte/yugabyte-db`, then apply that modified patch in `yugabyte/yugabyte-db`.
It is quite hacky, and it doesn't preserve [Git author information](#git-author-information).

Perhaps the best way to do it is using subtree merge.
This requires having the source repository's commit present in the destination repository.
Then, a cherry-pick can be done directly.

For example, suppose you put up a GitHub PR of a commit that you point-imported on `yugabyte/postgres`.
That commit exists locally in your `postgres` repository, and it also exists in your GitHub fork of `postgres`.
Either can be used as a remote in order to get the commit: for this example, let's use the fork as that process may be more familiar to people.

```sh
git remote add postgres-fork https://github.com/<my_user>/postgres
git fetch postgres-fork <commit>
git cherry-pick --strategy subtree -Xsubtree=src/postgres <commit>
```

This can get tedious in case you point-imported multiple commits at once, and you want to resolve all merge conflicts in one go rather than one-by-one.
In that case, you can construct a squash commit of the source commits, then subtree cherry-pick that.
For example:

```sh
pushd ~/code/postgres
git switch -c tmp-squash heads/yb-pg15
git merge --squash <feature_branch>
popd
git remote add local-postgres ~/code/postgres
git fetch local-postgres tmp-squash
git cherry-pick --strategy subtree -Xsubtree=src/postgres local-postgres/tmp-squash
```

In all cases, cross-repository merge conflicts may arise, in which case resolution details should be noted in the commit messages.
See MERGE.
TODO(jason): where is MERGE?

### Testing PostgreSQL

When doing a point-import or merge of upstream PostgreSQL, often that leads to creating a commit in `yugabyte/postgres` repo.
One should make sure that tests pass after that commit to catch logical merge conflicts.

There are many tests scattered around different Makefiles, generally run using `make check`.
Ideally, all should pass, but here are a few that one should pay special attention to since `yugabyte/yugabyte-db` also partially uses them:

```sh
./configure
make check
( cd contrib; make check )
( cd src/test/isolation; make check )
( cd src/test/modules; make check )
```

There are also `pg_dump` tests.
They are not run in `yugabyte/yugabyte-db`, but it is still good to make sure they pass:

```sh
./configure --enable-tap-tests
( cd src/bin/pg_dump; make check )
```

### Find PostgreSQL back-patch commits

When PostgreSQL back-patches a commit, the commit message is generally unchanged.
Therefore, one way to find all back-patches of a given commit is by filtering on the commit title: `git --grep '<title>' --all`.
Do be aware to neutralize any regex special characters when using this command, for example by replacing them with `.` or `.*` or omitting them if they are at one of the ends.
For example, title `Reject substituting extension schemas or owners matching ["$'\].` can be searched by `git --grep 'Reject substituting extension schemas or owners matching ' --all`.

If you already have a certain branch in mind, it is more efficient to search in that branch directly.
For example, if you are interested in finding whether a certain `master` commit has been back-patched to `REL_15_STABLE`, use `git --grep '<title>' REL_15_STABLE`.
If you are interested in finding whether a certain `REL_15_STABLE` back-patch commit is present in `REL_18_1`, use `git --grep '<title>' REL_18_1`.

[repo-yugabyte-db]: https://github.com/yugabyte/yugabyte-db
[repo-thirdparty]: https://github.com/yugabyte/yugabyte-db-thirdparty
[upstream-repositories-csv]: https://github.com/yugabyte/yugabyte-db/blob/master/src/lint/upstream_repositories.csv
