---
title: How to merge upstream repositories into YugabyteDB
headerTitle: How to merge upstream repositories into YugabyteDB
linkTitle: Merge with upstream repositories
description: How to merge upstream repositories into YugabyteDB
headcontent: How to merge upstream repositories into YugabyteDB
menu:
  stable:
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
- [Prerequisite packages or binaries](../build-from-src-almalinux).

This document is primarily concerned about the case of code being embedded into the [yugabyte-db repository][repo-yugabyte-db].

## Note to external contributors

Before getting started, note that most of this document is applicable to both internal and external contributors, but some steps are internal-only.
In particular, references to "Phorge", "`arc`", and "internal slack" are internal.
Such references generally pertain to putting up [yugabyte/yugabyte-db][repo-yugabyte-db] code for review, getting it tested through jenkins, and committing it.
External contributors may publish the code for review using a GitHub PR and reach out in our [community Slack]({{<slack-invite>}}) so that a YugabyteDB engineer can take care of the internal steps.

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

In case the commit being tracked is present in the external repository, this may seem trivial to do anyway without [`upstream_repositories.csv`][upstream-repositories-csv].
However, YugabyteDB occasionally takes point-imports of external repository commits, such as security fixes.
To make matters worse, those point-imports can have conflicts on the external repository side, and they could have other conflicts on the [yugabyte-db repository][repo-yugabyte-db] side as well.
Do that ten or so times, and it becomes difficult to track which changes are made by YugabyteDB and which changes are point-imported.
The CSV necessitates a single commit tracking what upstream state is being tracked.
Therefore, in case of port-imports, YugabyteDB must create a fork of the external repository to track the state of the upstream code with the point-imports.

To make matters more complicated, the upstream repository might be tracked differently across [yugabyte/yugabyte-db][repo-yugabyte-db] branches.
For example, pgaudit has different branches for each PG version while pg_cron has a single branch where, at any point, there is multi-version support.
In case of the former, it is necessary to track separate upstream commits across [yugabyte/yugabyte-db][repo-yugabyte-db] branches, and if a fork is required, then a branch should be created for each PG version that YugabyteDB supports, conventionally named `yb-pg<version>` for `master` and `yb-pg<version>-<yb_branch>` otherwise.
In case of the latter, you may have to do the same in case older branches diverge from the master.
Otherwise, a single `yb` branch can be used across all [yugabyte/yugabyte-db][repo-yugabyte-db] branches provided the PG versions of those branches are all within support.
Typically, this is possible for low-traffic third-party extensions.
Branches can be renamed, so if the single `yb` branch ever needs to be split to two, it can switch to the `yb-pg<version>` format from that point.
It's fine as long as historical commits are not lost so that old commits still have a valid upstream commit that can be referenced.

Not all external repositories are yet tracked in [`upstream_repositories.csv`][upstream-repositories-csv].
New repositories should be registered in here.
Old repositories should be migrated into the CSV over time.
Today, all repositories under `src/postgres` are tracked in the CSV.
By using the CSV, several linter rules have been made to ensure YugabyteDB stays aligned with the upstream repositories.
Note: as of today, there is no foolproof linter check that the CSV is updated properly whenever a merge with an upstream repository is made ([issue #27023](https://github.com/yugabyte/yugabyte-db/issues/27023) may help close that gap).

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
  This makes `git blame` lookup more convenient.
- Direct merge commits.
  Not only is performing the merge easier and less prone to error, but the history it leaves behind is clearer and easier to review.

Cons of using git subtrees over squash merge:

- Get all upstream commit history.
  This is listed as both a pro and con, the con being it can bloat the [yugabyte/yugabyte-db][repo-yugabyte-db] repository.
- More complex process which involves more steps.
- `git subtree` is not built into `git` by default.
- Git commands using paths do not work well with subtrees.
  For example, with the squash merge embedding strategy, `git log -- src/postgres/third-party-extensions/postgresql-hll` lists all commits touching that path.
  However, with the subtree embedding strategy, `git log -- src/postgres/third-party-extensions/documentdb` does not list such commits.
  And instead, `git log -- documentdb_errors.csv` lists those commits.
  Which is to say, the paths of all these upstream repositories are mapped to the same base.
  So common paths such as `git log -- .gitignore` show a mix of all `.gitignore` changes across the upstream repositories using subtrees.
- [GitHub issues may be inadvertently closed.](#github-closing-issues-via-commit-messages)

There is currently no strong preference for one over the other, though currently, upstream repositories have only migrated from squash merge to subtree, never the other way around.

### Adding a new upstream repository as subtree

When trying to introduce a new upstream repository using the subtree embedding strategy, it is pretty straightforward.

The following steps detail what has already been done to add DocumentDB.

1. Find an appropriate upstream repository commit to target.
   Normally, this would correspond to a Git tag as an official release for stability.
1. Fetch that tag to [yugabyte/yugabyte-db][repo-yugabyte-db].

   ```sh
   git remote add documentdb https://github.com/microsoft/documentdb.git
   git fetch documentdb tags/v0.102-0:tags/v0.102-0 --no-tags
   ```

   In case of a conflict with existing tags, there are many workarounds, one of which is to delete the existing tag first then re-run the fetch.
1. Find an appropriate location to embed the repository.
   The common case is PostgreSQL extensions, which belong in `src/postgres/third-party-extensions/<extension_name>`.
1. Switch to a new feature branch, based off the latest [yugabyte/yugabyte-db][repo-yugabyte-db] `master` branch.
1. Add the subtree.

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
1. Add the upstream repository details into [`upstream_repositories.csv`][upstream-repositories-csv].
   Commit this single change as one commit on top of the subtree merge commit.
   In this example, this was done by commit [3201d6f146c957b39ce23f3c59e76ca31ffa1a0c](https://github.com/yugabyte/yugabyte-db/commit/3201d6f146c957b39ce23f3c59e76ca31ffa1a0c).
1. Submit this feature branch as reference for review.
   Phorge is not sufficient since it strips Git metadata.
   One example of a place to submit this is a personal GitHub fork of [yugabyte/yugabyte-db][repo-yugabyte-db].
   The reviewer should make sure the subtree merge is done properly and that exactly one commit is added after that concerning [`upstream_repositories.csv`][upstream-repositories-csv].
1. Create a Phorge revision of this feature branch for official review.
   Make sure lint is not skipped to make sure [`upstream_repositories.csv`][upstream-repositories-csv] is valid.
   The reviewer should make sure the latest commit hash of both submissions are exactly equal.
1. Request permission from `@buildteam` in the internal slack `#eng-infra` channel to land a merge commit of this revision.
1. Land the change using the merge strategy:

   ```sh
   arc land --strategy merge --revision D...... --onto master
   ```

   Append `--hold` if it is your first time and you are afraid of making mistakes.
   After [checking the structure looks good](#subtree-illustrations) using a command such as `git log --oneline --graph`, push the change using the sample command provided at the end of the `arc land --hold` output.
   In this example, this was done by commit [b01e29bafa6fb8bfb899f0b3ac6e98363340c8e7](https://github.com/yugabyte/yugabyte-db/commit/b01e29bafa6fb8bfb899f0b3ac6e98363340c8e7).
1. [Reopen any GitHub issues that were inadvertently closed.](#github-closing-issues-via-commit-messages)

### Converting a squash embedded upstream repository to subtree

Working with an existing squash embedded repositories to the subtree strategy is a lot more complicated.
This is primarily because of the hoops you have to jump through to make `git blame` point to the subtree rather than the old squash commits.

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
1. The remaining steps are equivalent to [adding a new upstream repository as subtree](#adding-a-new-upstream-repository-as-subtree), starting from adding repository details to [`upstream_repositories.csv`][upstream-repositories-csv].
   There is no example commit for the [`upstream_repositories.csv`][upstream-repositories-csv] change since it did not exist at the time.
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
1. The remaining steps are equivalent to [before](#adding-a-new-upstream-repository-as-subtree), starting from adding repository details to [`upstream_repositories.csv`][upstream-repositories-csv].
   There is no example commit for the [`upstream_repositories.csv`][upstream-repositories-csv] change since it did not exist at the time.
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

1. If the upstream repository is currently not forked by YugabyteDB, ask yourself whether this point-import is necessary: if you instead merge to a newer version, you avoid the need to make a YugabyteDB fork.
   In this example, there already exists a [yugabyte/postgres][repo-postgres] fork.
1. In case there's no YugabyteDB fork, that needs to be created and [a new branch added to it](#how-upstream-repositories-are-tracked).
   Again, [yugabyte/postgres][repo-postgres] fork already exists.
   If using pgaudit as an example instead, `git switch -c yb-pg<version> "$(grep pgaudit src/lint/upstream_repositories.csv | cut -d, -f4)"`.
1. Switch to a feature branch off the commit listed in [`upstream_repositories.csv`][upstream-repositories-csv].
   In this example, that should be equivalent to the latest [yugabyte/postgres][repo-postgres] repo `yb-pg<version>` branch: `git switch -c import-abc yb-pg<version>`.
1. Find the appropriate upstream commits.
   For example, if this import is for [yugabyte/yugabyte-db][repo-yugabyte-db] repo `2025.1` branch based on PG 15.x, then prioritize using upstream postgres commits on `REL_15_STABLE` over those on `master` because they would have resolved conflicts for us.
   See [here](#find-postgresql-back-patch-commits) for suggestions on how to do this.
   If there are too many upstream commits to consider, it may be worth looking into the alternative of taking file states as of a certain commit instead.
1. Do `git cherry-pick -x <commit>` for each commit being imported.
   Notice the `-x` to record the commit hash being cherry-picked.
   For any merge conflicts, resolve and amend them to that same commit, describing resolutions within the commit messages themselves.
   This includes logical merge conflicts which can be found via compilation failure or test failure.
   In case of authoring new commits (for example, taking file states as of a certain commit), start the commit with `YB:`, and properly document the actions taken.
   Make sure [tests pass](#testing-postgresql) at each individual commit.
   At the end, you should be n commits ahead of the commit in [`upstream_repositories.csv`][upstream-repositories-csv], where n is the number of commits you are point-importing, unless more new commits are authored.
   Make a GitHub PR of this through your fork for review.
1. On the [yugabyte/yugabyte-db][repo-yugabyte-db] repo, import the commits that are part of the [yugabyte/postgres][repo-postgres] repo PR.
   This is not as straightforward as a cherry-pick since it is across different repositories: see [cross-repository cherry-pick](#cross-repository-cherry-pick) for advice.
   Unlike as in [yugabyte/postgres][repo-postgres], the commit structure in [yugabyte/yugabyte-db][repo-yugabyte-db] does not matter since these changes will eventually be squashed.
   Once all commits are imported, update [`upstream_repositories.csv`][upstream-repositories-csv] and create a Phorge revision.
   The Phorge summary should have the upstream postgres commit hashes and their commit messages.
   In case of YB's point-imports, provide both the cherry-pick's hash and the original commit's hash.
   Add to the summary the merge resolution details that were previously recorded.
1. Pass review for both the [yugabyte/postgres][repo-postgres] imports and the [yugabyte/yugabyte-db][repo-yugabyte-db] revision.
1. Land the [yugabyte/yugabyte-db][repo-yugabyte-db] revision.
   **If there is a merge conflict on [`upstream_repositories.csv`][upstream-repositories-csv], then redo the whole process.**
   It means someone else updated `yb-pg<version>` branch, so [yugabyte/postgres][repo-postgres] cherry-picks need to be rebased on latest `yb-pg<version>`, compilation/testing needs to be re-done, and since the commit hashes change, [`upstream_repositories.csv`][upstream-repositories-csv] needs a new commit hash.
   Plus, it's safest to re-run tests on [yugabyte/yugabyte-db][repo-yugabyte-db] after all this adjustment.
1. Push the [yugabyte/postgres][repo-postgres] commits directly onto `yb-pg<version>`: `git push <remote> import-abc:yb-pg<version>`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on [yugabyte/yugabyte-db][repo-yugabyte-db] [`upstream_repositories.csv`][upstream-repositories-csv], and if landing that change passes, then no one should have touched `yb-pg<version>` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg<version>`.**
1. If backporting to stable branches,

   - ...if the PG version is the same, the backport can be performed just like any other backport by cherry-picking the original commit.
   - ...if the PG version is different and a single multi-PG-version compatible `yb` branch is used in the upstream repository, same thing.
   - ...otherwise, the same process should be repeated using the `yb-pg<version>` branch corresponding to that stable branch's PG version.
     In particular, the [search for back-patch commits](#find-postgresql-back-patch-commits) should be redone for that PG version.

#### Squash direct-descendant merge

A direct-descendant merge for PostgreSQL is typically a minor version upgrade.
An example is PG 15.2 to 15.12 as done in [yugabyte/postgres@12398eddbd531080239c350528da38268ac0fa0e](https://github.com/yugabyte/postgres/commit/12398eddbd531080239c350528da38268ac0fa0e) and [yugabyte/yugabyte-db@e99df6f4d97e5c002d3c4b89c74a778ad0ac0932](https://github.com/yugabyte/yugabyte-db/commit/e99df6f4d97e5c002d3c4b89c74a778ad0ac0932).

1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb-pg15` branch since a `postgres/postgres` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut could not be used.)
1. Otherwise, switch to a feature branch off the latest [yugabyte/postgres][repo-postgres] repo `yb-pg<version>` branch: `git switch -c merge-pg yb-pg<version>`.
   Then, do `git merge REL_15_12`.
   (It may still be the case that the target version, in this example 15.12, already has contains all the changes YB imported on top of 15.2.
   If that is the case, then the merge can be simplified to take exactly the target version: `git merge -X theirs REL_15_12`.
   In this example, that was not possible since YB imported some newer commits.)
   [Make sure tests pass](#testing-postgresql).
   Record merge resolutions into the merge commit message.
   There should only be the merge commit and no other extraneous commits on top of it.
   Make a GitHub PR of this through your fork for review.
1. Apply those changes to the [yugabyte/yugabyte-db][repo-yugabyte-db] repo.
   See [cross-repository cherry-pick](#cross-repository-cherry-pick) for details.
   Update [`upstream_repositories.csv`][upstream-repositories-csv], and create a Phorge revision listing all the merge conflict details.
   Ensure that the [author information](#git-author-information) for the latest commit of this Phorge revision is `YugaBot <yugabot@users.noreply.github.com>`.
1. Pass review for both [yugabyte/postgres][repo-postgres] GitHub PR and [yugabyte/yugabyte-db][repo-yugabyte-db] Phorge revision.
   If [yugabyte/postgres][repo-postgres] needs changes, then commit hashes change, so [yugabyte/yugabyte-db][repo-yugabyte-db] [`upstream_repositories.csv`][upstream-repositories-csv] must be updated, and re-test on [yugabyte/yugabyte-db][repo-yugabyte-db] should be considered.
1. Land the [yugabyte/yugabyte-db][repo-yugabyte-db] revision.
   **If there is a merge conflict on [`upstream_repositories.csv`][upstream-repositories-csv], then redo the whole process.**
   It means someone else updated `yb-pg15` branch, so [yugabyte/postgres][repo-postgres] merge need to be redone on latest `yb-pg15`, compilation/testing needs to be re-done, and since the commit hashes change, [`upstream_repositories.csv`][upstream-repositories-csv] needs a new commit hash.
   Plus, it's safest to re-run tests on [yugabyte/yugabyte-db][repo-yugabyte-db] after all this adjustment.
1. Push the [yugabyte/postgres][repo-postgres] merge directly onto `yb-pg15`: `git push <remote> merge-pg:yb-pg15`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on [yugabyte/yugabyte-db][repo-yugabyte-db] [`upstream_repositories.csv`][upstream-repositories-csv], and if landing that change passes, then no one should have touched `yb-pg15` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg15`.**

#### Squash non-direct-descendant merge

A non-direct-descendant merge for PostgreSQL is typically a major version upgrade.
An example is PG 11.2 to 15.2 as _initially_ done in [yugabyte/yugabyte-db@55782d561e55ef972f2470a4ae887dd791bb4a97](https://github.com/yugabyte/yugabyte-db/commit/55782d561e55ef972f2470a4ae887dd791bb4a97).
Note that this was done before [`upstream_repositories.csv`][upstream-repositories-csv] was in place, so it lacks formality and should not be looked at as a model example.

This merge can technically be handled similarly as [direct-descendant merge](#squash-direct-descendant-merge).
However, there is a much faster alternative that should be taken.
A prerequisite for using this alternative is that the target version contains all the commits of the source version (or, nearly equivalently, the target version was released later than the source version).
For example, PG 15.2 should contain all commits in PG 11.2, considering backports as equivalent to each other, and for any commits only in PG 11.2, they are likely irrelevant for PG 15.2.
Another example, PG 15.12 to PG 16.1 cannot use this alternative strategy since 15.12 has backports from master that 16.1 does not have.
As a rule of thumb, if the source version release date is before the target version release date by at least a few days, the prerequisite is likely met.
This prerequisite generally shouldn't fail since major version upgrades should aim for the latest minor version of a major version.

The steps below detail a hypothetical merge of PG 15.12 to PG 18.1.
At the time of writing, YugabyteDB is based off PG 15.12.

1. Ensure PG 15.12 to PG 18.1 satisfies the prerequisite mentioned above.
1. On the [yugabyte/postgres][repo-postgres] repo, identify the point-imports or custom changes on `yb-pg15` that need to be carried over to PG 18.1.
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
1. Do the same steps as in [direct-descendant merge](#squash-direct-descendant-merge), starting from "Apply those changes to the [yugabyte/yugabyte-db][repo-yugabyte-db] repo."
   A difference is that this merge may be so much larger that the initial merge is incomplete and potentially based off an old commit on [yugabyte/yugabyte-db][repo-yugabyte-db].
   This initial merge commit and further development to close the gaps can be done in a separate `pg18` branch on [yugabyte/yugabyte-db][repo-yugabyte-db].
   This is what was done for the PG 15 merge, from [yugabyte/yugabyte-db@55782d561e55ef972f2470a4ae887dd791bb4a97](https://github.com/yugabyte/yugabyte-db/commit/55782d561e55ef972f2470a4ae887dd791bb4a97) to [yugabyte/yugabyte-db@eac5ed5d186b492702a0b546bf82ed162da506b0](https://github.com/yugabyte/yugabyte-db/commit/eac5ed5d186b492702a0b546bf82ed162da506b0).

### Embedded via subtree

#### Subtree point-imports

1. Follow the [steps for squash point-imports](#squash-point-imports) up until modifying the [yugabyte/yugabyte-db][repo-yugabyte-db] repo.
1. On the [yugabyte/yugabyte-db][repo-yugabyte-db] repo, [subtree merge](#git-subtrees) this branch.
1. If backporting to stable branches,

   - ...if the PG version is the same, the previously constructed subtree commit can be reused for [subtree merge](#git-subtrees).
   - ...if the PG version is different and a single multi-PG-version compatible `yb` branch is used in the upstream repository, same thing.
   - ...otherwise, the same process should be repeated using the `yb-pg<version>-<yb_branch>` branch corresponding to that stable branch.

#### Subtree direct-descendant merge

The steps that follow will use documentdb as a hypothetical example.
Suppose that YugabyteDB was based off a yugabyte/documentdb repo `yb` branch constructed from `v0.102-0` with commit [b896737f53d9eb13e0b397976e1b2edd310cca57](https://github.com/microsoft/documentdb/commit/b896737f53d9eb13e0b397976e1b2edd310cca57) cherry-picked.
We are trying to merge to `v0.103-0` (which doesn't exist at the time of writing, but you get the idea of the example).

1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb` branch since a `microsoft/documentdb` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut can not be used.)
1. Otherwise, switch to a feature branch off the latest yugabyte/documentdb repo `yb` branch: `git switch -c merge-documentdb yb`.
   Then, do `git merge v0.103-0`.
   (It may still be the case that the target version, in this example `v0.103-0`, already has contains all the changes YB imported on top of `v0.102-0`.
   If that is the case, then the merge can be simplified to take exactly the target version: `git merge -X theirs v0.103-0`.)
   Do any relevant testing if applicable.
   Record merge resolutions into the merge commit message.
   There should only be the merge commit and no other extraneous commits on top of it.
   Make a GitHub PR of this through your fork for review.
1. On the [yugabyte/yugabyte-db][repo-yugabyte-db] repo, [subtree merge](#git-subtrees) this branch.

#### Subtree non-direct-descendant merge

The steps that follow will use pgaudit as an example.

This merge can technically be handled similarly as [direct-descendant merge](#squash-direct-descendant-merge).
See [squash non-direct-descendant merge](#squash-non-direct-descendant-merge) for whether an alternate strategy can be used instead, then proceed below.

Suppose that YugabyteDB was based off a yugabyte/pgaudit repo `yb-pg11` branch constructed from `1.3.2` with commit [455cde5ec3a4374b18ad551aaabe6d60761b6503](https://github.com/pgaudit/pgaudit/commit/455cde5ec3a4374b18ad551aaabe6d60761b6503) cherry-picked.
We are trying to merge to `1.7.0`.

1. Ensure `1.3.2` to `1.7.0` satisfies the prerequisite mentioned above.
1. First, determine whether a merge in the upstream repository can be skipped.
   This is only permissible if the current upstream repository referenced commit is not a YugabyteDB commit (e.g. there is no `yb-pg11` branch since a `pgaudit/pgaudit` commit is used directly).
   Then, the target version can directly be used.
   (In this example, this shortcut can not be used.)
1. Otherwise, follow the steps to identify commits to cherry-pick and create a `yb-pg15` branch as in [squash non-direct-descendant merge](#squash-non-direct-descendant-merge).
1. On the [yugabyte/yugabyte-db][repo-yugabyte-db] repo, [subtree merge](#git-subtrees) this branch.

## General advice

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

For Phorge, the author information of the latest commit being landed is transferred to the merge or squash commit constructed by `arc land`.

### GitHub closing issues via commit messages

GitHub automatically [closes issues based on keywords](https://github.blog/news-insights/product-news/closing-issues-via-commit-messages/), and subtree commits may have such messages.
The problem is that those issue number references are for the subtree's repository, but they end up closing [yugabyte/yugabyte-db][repo-yugabyte-db] repository issues.
This isn't too big of a problem since most of YugabyteDB's issues on low numbers are already closed, and these subtrees's issue numbers tend to be relatively low.
However, when it does happen, the issues that were inadvertently closed need to manually be reopened.

### Git multiple remotes

When it comes to merging upstream repositories, you will likely have to manage multiple remote repositories.
This section illustrates a workflow for that using the PostgreSQL repository as an example.

{{< tip title="Tip" >}}
It is highly recommended that you get a good understanding of git remotes.
Refer to [this article](https://dev.to/hashcode01/add-a-second-remote-origin-to-git-35a7) for the concepts.
{{< /tip >}}

YugabyteDB's [yugabyte/postgres][repo-postgres] repository does not have all branches of upstream PostgreSQL, nor is it guaranteed to be synced.
The common case workflow is to have at least two remotes: one for [yugabyte/postgres][repo-postgres] and one for upstream PostgreSQL.
The upstream PostgreSQL remote can be set up in one of the following ways:

- HTTP protocol: `https://git.postgresql.org/git/postgresql.git`
- HTTP protocol (mirror): `https://github.com/postgres/postgres`

You fetch commits from the upstream PostgreSQL repository and perform your work on branches that base off commits in the [yugabyte/postgres][repo-postgres] repository.
An example `git remote -v` output after you set up the two remotes is as follows:

```
pg      https://git.postgresql.org/git/postgresql.git (fetch)
pg      https://git.postgresql.org/git/postgresql.git (push)
yb      ssh://<user>@github.com/yugabyte/postgres (fetch)
yb      ssh://<user>@github.com/yugabyte/postgres (push)
```

### Find PostgreSQL back-patch commits

First, [fetch all PostgreSQL commits](#git-multiple-remotes) so that they are available for local search.

When PostgreSQL back-patches a commit, the commit message is generally unchanged.
Therefore, one way to find all back-patches of a given commit is by filtering on the commit title: `git --grep '<title>' --all`.
Do be aware to neutralize any regex special characters when using this command, for example by replacing them with `.` or `.*` or omitting them if they are at one of the ends.
For example, title `Reject substituting extension schemas or owners matching ["$'\].` can be searched by `git --grep 'Reject substituting extension schemas or owners matching ' --all`.

If you already have a certain branch in mind, it is more efficient to search in that branch directly.
For example, if you are interested in finding whether a certain `master` commit has been back-patched to `REL_15_STABLE`, use `git --grep '<title>' REL_15_STABLE`.
If you are interested in finding whether a certain `REL_15_STABLE` back-patch commit is present in `REL_18_1`, use `git --grep '<title>' REL_18_1`.

### Testing PostgreSQL

When doing a point-import or merge of upstream PostgreSQL, often that leads to creating a commit in [yugabyte/postgres][repo-postgres] repo.
You should make sure that tests pass after that commit to catch logical merge conflicts.

There are many tests scattered around different Makefiles, generally run using `make check`.
Ideally, all should pass, but here are a few that you should pay special attention to since [yugabyte/yugabyte-db][repo-yugabyte-db] also partially uses them:

```sh
./configure
make check
( cd contrib; make check )
( cd src/test/isolation; make check )
( cd src/test/modules; make check )
```

There are also `pg_dump` tests.
They are not run in [yugabyte/yugabyte-db][repo-yugabyte-db], but it is still good to make sure they pass:

```sh
./configure --enable-tap-tests
( cd src/bin/pg_dump; make check )
```

### Cross-repository cherry-pick

When using the squash embedding strategy, changes to an upstream repository need to be transferred to [yugabyte/yugabyte-db][repo-yugabyte-db] manually.
This is a cross-repository cherry-pick.

Cherry-picking a change from one repository to another where the prefixes of paths differ is a little tricky.
It is most commonly done for point-imports of upstream repositories to YugabyteDB.

If you are new to it, you may be tempted to copy-paste code changes from one side to the other.
Please do not do that as it is error-prone.

A smarter way to do it is to get a patch of the upstream changes, prefix all paths to the appropriate location in [yugabyte/yugabyte-db][repo-yugabyte-db], then apply that modified patch in [yugabyte/yugabyte-db][repo-yugabyte-db].
It is quite hacky, and it doesn't preserve [Git author information](#git-author-information).

The best way to do it is using the subtree merge strategy.
This requires having the source repository's commit present in the destination repository.
Then, a cherry-pick can be done directly.

For example, suppose you put up a GitHub PR of a commit that you point-imported on [yugabyte/postgres][repo-postgres].
That commit exists locally in your `postgres` repository, and it also exists in your GitHub fork of `postgres`.
Either can be used as a remote in order to get the commit: for this example, let's use the fork as that process may be more familiar to people.

```sh
git remote add fork-pg https://github.com/<your_username>/postgres
git fetch fork-pg <full_commit_hash>
git cherry-pick --strategy subtree -Xsubtree=src/postgres <full_commit_hash>
```

This can get tedious in case you point-imported multiple commits at once, and you want to resolve all merge conflicts in one go rather than one-by-one.
In that case, you can construct a squash commit of the source commits locally, then subtree cherry-pick that.
For example:

```sh
pushd ~/code/postgres
git switch -c tmp-squash heads/yb-pg15
git merge --squash <feature_branch>
popd
git remote add local-pg ~/code/postgres
git fetch local-pg tmp-squash
git cherry-pick --strategy subtree -Xsubtree=src/postgres local-postgres/tmp-squash
```

In case of larger direct-descendant and non-direct-descendant merges, cherry-picking merge commits is still possible using the `-m` option.
However, that should be weighed against an alternate approach to do an actual merge on the upstream repository's side, then transfer the changes over to [yugabyte/yugabyte-db][repo-yugabyte-db]:

1. On the upstream repository commit before the upstream merge (which should equal the commit recorded in [yugabyte/yugabyte-db][repo-yugabyte-db] [`upstream_repositories.csv`][upstream-repositories-csv]), sync the content of [yugabyte/yugabyte-db][repo-yugabyte-db]'s upstream repository directory here.
   YugabyteDB may have introduced new files, which can be ignored for now and merged properly later.
   What we care about for the merge are existing files that have been overwritten with new content.
   Use `git add -u` to add all those changes, and commit them (commit message does not matter since this is not an official commit).
1. `git merge` the upstream merge commit.
   Since this is a merge commit, `git blame` shows upstream commits on one side of the conflict, and on the other side, it shows the YB changes squashed to a single commit created in the previous step.
   For further reference of the YB changes, consult `git blame` in [yugabyte/yugabyte-db][repo-yugabyte-db].
   As always, keep track of resolution notes somewhere.
1. After completing the unofficial merge to the best of your ability, sync this new content back to [yugabyte/yugabyte-db][repo-yugabyte-db]'s upstream repository directory.
   Now, build can be attempted in [yugabyte/yugabyte-db][repo-yugabyte-db] to catch compilation and test issues, leading to further changes.

In all cases, cross-repository [merge conflicts](#merge) may arise, in which case resolution details should be noted in the commit messages.

### Merge

There are many resources out there to help with merging code.
Some of this information may be a repeat or refresher.

A merge conflict happens due to at least one historical commit on one side.

Consider the following example:

```
A -> B -> C -> D -> E -> ... -> Y
            \
             > D' -> E' -> ... -> J'
```

Suppose the prime branch was merged to the main branch:

```
A -> B -> C -> D -> E -> ... -> Y ---> Z
            \                       /
             > D' -> E' -> ... -> J'
```

In the above example, `Z` is a merge of `Y` and `J'`.
If there is a merge conflict, then the reason must be found in at least one of `D,E,...,Y,D',E',...,J'`.

Suppose instead, commit `Q` was cherry-picked onto the prime branch:

```
A -> B -> C -> D -> E -> ... -> Y
            \
             > D' -> E' -> ... -> J' -> Q'
```

In the above example, `Q'` is a cherry-pick of `Q`.
If there is a merge conflict, then the reason must be found in at least one of `D,E,...,P,D',E',...,J'`.
Notice that `Q` is not considered a historical commit for the merge conflict reason.

In an ideal scenario, all related historical commits are investigated, then the appropriate action is taken in context of that.
However, it can be an insurmountable task to find all such commits in a large codebase such as PostgreSQL.
So take a best effort approach in investigating a conflict.

- If a merge conflict appears trivial, you may resolve it directly without looking up the history.
- In the general case, use `git blame` on each side of a conflict hunk to find the offending commit(s).
  Sometimes, more digging is necessary by walking further up the history (for example, if the main change was shadowed by a style change or refactor such as moving a function from one file to another).
- Finding historical commits that deleted lines is harder than those that added lines since deleted lines do not show up in `git blame`.
  There is no need to go out of your way to look for these historical commits unless you suspect one exists (due to the fundamental fact that conflicts always come from at least one historical commit).

In case you are doing a point-import and the same historical commit appears multiple times as a reason for the conflict, consider importing that historical commit as it may save some hassle and make review easier as well.

#### Finding conflict-causing commits

In case of `git merge`, `git blame` works on both sides of each conflict hunk.
In case of `git cherry-pick`, `git blame` does not work on the side for the commit being cherry-picked.
To work around that, the simplest way is to make a second checkout of the repository at the commit being cherry-picked, then investigate using `git blame` and other tools from there.
Otherwise, you can still see the state of the file at that commit using `git show <commit>:<path>`, find commits that changed line XYZ using some `git log -G`, etc., but it is not as convenient.

In the common case, `git blame` on lines within conflict markers is good enough to reveal the offending commit(s).
In case it is not enough, consider the following tips:

- In case `git blame` points to a merge commit (authored by YugabyteDB), that merge commit should have listed the commit hashes involved in the conflict in an easy-to-find fashion.
  Continue the search using those commits.
- In case lines are missing, `git blame` does not help.
  Instead, you can search for commits that touched that file using `git log --patch <revision-range> -- <path>`.
- If you have any good keywords related to the conflict, such as "MinimalTuple", you can look up commits related to that using `git log --grep <keyword> <revision-range>`.
  You may even be interested in looking up the keyword in historical merge commits in case there were similar conflicts whose resolutions you can follow.
  In any case, the lookup can be made faster by specifying some `-- <path>` to the command: for YugabyteDB, `-- src` would exclude documentation changes, for example.
- If you have any good code text related to the conflict, such as "IS_AF_UNIX", you can look up commits touching lines containing that text using `git log -S <code_text> <revision-range>` or `git log -G <code_text> <revision-range>` (read the manual for more details between the two).
  Again, lookup can be made faster by specifying some `-- <path>`.

#### Merge commit message

When using `git merge`, the commit title is initially automatically generated.
This depends on what ref was specified to `git merge` and what branch was checked out at the time.
The template looks something like `Merge <ref> into <branch>`.
For example,

- `Merge commit 'd4103e809e0662c5cd4a51f9dfafb44ce210d38c' into pg15-master-merge`
- `Merge tag 'YB_VERSION_4_9_3' into yb-orafce`

The message is definitely changeable, but it may be of best interest to make sure that the auto-generated message appears like you wish it to be as a sanity check that you are doing things correctly.

For the ref, a **full** commit hash is preferred over short commit hashes, which have a risk of collision.
Tags are preferred since they are more human-readable, but they should not be used if they may be deleted in the future.
In any case, the merge commit's second parent is the commit being merged, so this is all technically duplicate information.
A reason to still care about it is that Phorge merge commit titles are derived from these titles, and the Phorge merge commit is more removed from the commit being merged.

This brings us to the next topic: do not use the template `Merge <ref> into <branch>` if ref or branch are not accurate.
For example, when working on the PG 15 merge, a `pg15` branch was created to run in parallel with the `master` branch.
Periodically, a commit from `master` was merged into `pg15` as follows:

1. `git switch -c pg15-master-merge pg15`
1. `git merge <master_commit_full_hash>`
1. Resolve conflicts, recording them in a temporary place, then `git merge --continue` and dump the resolution information to the commit message.
   The auto-generated title should be good as is.
1. Create a Phorge revision of this for review.
   Change the title to `[pg15] merge: master commit '<master_commit_full_hash>' into pg15`.
   Note that this title does not follow the template because the eventual merge commit when it lands will not be merging `<master_commit_full_hash>` into `pg15` but rather the previous merge commit into `pg15`.
   For a reader of `git log --first-parent pg15`, it is more useful to read that `<master_commit_full_hash>` is merged rather than some commit hash of a merge commit, which would be the case if we were to follow the template.

Here is a graphical representation of the above example (`git log --oneline --graph 71ebe84823510774407c82e015eab1d44925b642`):

```
*   71ebe84823 [pg15] merge: master branch commit '1d410d7ca552b89b5c87ca073f1fb4c8cf42957f' into pg15
|\
| *   168e90a4fc Merge commit '1d410d7ca552b89b5c87ca073f1fb4c8cf42957f' into pg15-master-merge
| |\
| | * 1d410d7ca5 (tag: 2.21.1.0-b67) [#14025] YSQL: import Add function to log the memory contexts of specified backend process.
| | * 997b064454 [#20950] DocDB: Update pg_cron to Jan 2024
| | * 5b3b972fa0 [PLAT-12406] Migrating replicated config with correct types
| | * 59dc0436b8 (tag: 2.21.1.0-b66) [PLAT-12529]: Add check for locale in node-agent preflight checks
| | * b0c142e4f7 [PLAT-12414] Faster incremental sbt builds for developers
| | * a3244a5981 [PLAT-12394] Change Gflag flow for YBM use case
* | | 586dfd99e3 [pg15] refactor: rework index concurrent computation
* | | 0a5cbae49f [pg15] fix: start DDL transaction for VACUUM ANALYZE
* | | 522642f1e3 [pg15] fix: passwordcheck extension
|/ /
* | fab65ca9cf [pg15] feat: activate 'Allow ALTER TYPE to change some properties of a base type'
* | 6192cad962 [pg15] test: fix expected value of reltuples in testCountAfterTruncate
* |   32e3ed9655 [pg15] merge: master branch commit '03c9c4ca25ba365f9deeba3cc66fb64f919c98d3' into pg15
|\ \
| * | c9d333c33d Merge commit '03c9c4ca25ba365f9deeba3cc66fb64f919c98d3' into pg15-master-merge
|/| |
| |/
| * 03c9c4ca25 (tag: 2.21.1.0-b65) [#20568][#20567] YSQL: Fix flakiness in some tests that use fail on conflict
| * e0593bc6a1 [#20389] YSQL, Backups: Implement new tablespace related flags in yb_backup
| * b0c02d75fa [PLAT-12527] [Master] Disable leaderlessTablet check on RF1 clusters
...
| * 988a6d4363 (tag: 2.21.1.0-b2) [PLAT-12412] API: Generate server stubs using openapi
| * 1ded410bf1 [PLAT-12396] Reset Provider usability state in case edit task creation fails
| * e6ee3d39e9 [PLAT-12360] Create provider with the same namespace as the CR object
* | 1d332e208f [pg15] fix: re-merge pgstat.c, wait_event.c
* | bb34506ee8 [pg15] fix: scan slot type in foreign scan
* | 020353c365 [pg15] fix: reset reltuples to -1 on TRUNCATE
* | 7d7872ebf2 [pg15] test: fully port yb_pg_insert regress test
* | 4eea368075 [pg15] fix: test_lint.sh, and test failing lint
* | dbb14f2886 [pg15] fix: CREATE TABLE LIKE INCLUDING DEFAULTS
* |   f399fdbdf0 [pg15] merge: master branch commit 'ba3761c73a7db1473141cc86c7a1fd033755102d' into pg15
|\ \
| * | 26ba6082b6 Merge commit 'ba3761c73a7db1473141cc86c7a1fd033755102d' into pg15-master-merge
| |\|
| | * ba3761c73a (tag: 2.21.1.0-b1) [#20709] YSQL: fix wrong results from index aggregate
| | * 3b0e58ba4b [PLAT-12318] Update YbUniverseReconciler to call edit universe on disk size, resource changes
| | * fad54f1a27 PITR remove Stop workloads warning (#20771)
```

In case of subtree merge commits, there are two additional considerations:

- If merge commit parent swapping is employed, then `<ref>` and `<branch>` should likewise be swapped in the title.
- `<ref>` should be clarified with the subtree repository name prefixed.
  This is particularly useful in case the same tag name is present in YugabyteDB and the subtree repository.

See the latest three commits of the following example (`git log --oneline --graph cd2bd24a6b5b9b0052655d40853eb0fe207707b1`):

```
*   cd2bd24a6b [pg15] merge: pg_stat_monitor tag '2.0.4' into pg15
|\
| *   481d26bf02 Merge pg15-pg-stat-monitor-merge into pg_stat_monitor tag '2.0.4'
| |\
| | *   d57d3b411c Merge pg15 into pg_stat_monitor commit 'e8bfff127b9bbadf1360469c06ea8f3cbde7e6fd'
| | |\
| * | | 75f86f54b1 Version bumped for the 2.0.4 release (#434)
| * | | 4863020ccd PG-646: pg_stat_monitor hangs in pgsm_store
| * | | 0a8ac38de9 Version bumped for the 2.0.3 release. (#430)
...  \ \
| * | | | 67b3d961ca PG-193: Comment based tags to identify different parameters.
| * | | | 89614e442b PG-190: Does not show query, if query elapsed time is greater than bucket time.
| | |/ /
| |/| |
| * | | e8bfff127b README Update.
| * | | 1dce75c621 README Update.
| * | | f42893472a PG-189: Regression crash in case of PostgreSQL 11.
...  /
| * | 56d8375c38 Issue - (#2): Extended pg_stat_statement to provide new features.
| * | f70ad5ad48 Issue - (#1): Initial Commit for PostgreSQL's (pg_stat_statement).
| * | 97c67356ca Initial commit
|  /
* | 29b46566b0 [pg15] test: fix yb_create_index
* | d1bc2d8c41 [pg15] test: port TestPgRegressProfile, yb_index_selectivity, yb_pg_insert_conflict
* |   7cbea2dcb5 [pg15] merge: master branch commit 'b66e31a7b1150c6921a7c0cf7be82af278a948a3' into pg15
|\ \
| |/
|/|
| * 6c6950f46c Merge commit 'b66e31a7b1150c6921a7c0cf7be82af278a948a3' into pg15-master-merge
|/|
| * b66e31a7b1 [#22387] YSQL: Fix Bitmap Scan CBO tests
| * 676eeada29 [##22388] YSQL: Change name of bitmap_exceeded_work_mem_cost
| * 0b8deec77a [#22364] YSQL: Enable CREATE/DROP ACCESS METHOD grammar
```

Note that `pg15-stat-monitor-merge` was a temporary branch on `d57d3b411c` at the time of doing the merge `481d26bf02`.
Specifying the temporary branch name in the commit title is fine since it conveys the intent and the metadata for the actual commit being merged is already contained in the commit itself.

As for the commit message body, look into the above examples as reference (e.g. `git show --no-patch 71ebe84823`).
Make sure that the information on what is being merged from what repository (if applicable) is clearly stated.

#### Cherry-pick commit message

When using `git cherry-pick -x`, the commit message is automatically generated.

In case of cherry-picking for the sake of officially pushing the cherry-picked commits directly (e.g. upstream repositories), if encountering any merge conflicts, put such details somewhere in the commit message.
In case of [cross-repository cherry-picking](#cross-repository-cherry-pick) and eventually landing a Phorge squash commit, concatenate the commit messages (including titles) and add merge conflict details somewhere in the Phorge summary (see [commit 9c084ca219411b6264aaab1dc15ab27adc74bf90](https://github.com/yugabyte/yugabyte-db/commit/9c084ca219411b6264aaab1dc15ab27adc74bf90) as an example).
The Phorge title should be of the form `[#<GH_issue>] YSQL: import <cherry-picked_commit_title>`, in case of cherry-picking a single commit, or `[#GH_issue>] YSQL: import <general_catchall>` otherwise.
In case of [cross-repository cherry-picking](#cross-repository-cherry-pick) an upstream merge commit, it may be more fitting to make the title and summary follow the format of a [merge commit message](#merge-commit-message).

#### Redoing a merge

##### Rerere

When redoing a merge, watch out for the rerere cache.
Git by default caches merge resolutions you previously did unless otherwise specified.
If you are not familiar with it, it is generally a bad thing as...

- ...you may have previously done an incorrect resolution, and this time, the incorrect resolution is automatically reapplied.
- ...these resolutions may be automatic for you but not for others, so you may miss documenting some merge conflict resolutions.

If you find yourself in this state, one way to delete the entire rerere cache is by `rm -rf .git/rr-cache` (if using Git worktrees, adjust accordingly).
Then, trying to `git merge` or `git cherry-pick` will bring you back to a clean view.

##### Rebase a merge commit

In case you are trying to rebase a merge commit to a newer base, consider the following steps:

1. `git merge` the newer base.
   Record any new conflicts, and try to also record any conflicts that disappeared since the previous merge, if applicable.
   You should now have two merge commits.
1. `git tag merge-backup` this state.
1. Redo the merge from the new base.
   Do not bother re-resolving conflicts.
   Instead, take the state as of `merge-backup`: `git diff -R merge-backup | git apply`.
   Add the changes and `git merge --continue`.
   Combine the resolution notes of the previous two merge commits.
   Remember to cancel out any conflicts that disappeared after the second merge commit.

#### Review

There are generally two kinds of things you will encounter in review: cherry-picks and merges.
Here are all the cases:

- [Squash point-import](#squash-point-imports)
  - Upstream repository to upstream repository: cherry-pick
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: cherry-pick
- [Squash direct-descendant merge](#squash-direct-descendant-merge)
  - Upstream repository to upstream repository: merge
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: cherry-pick
- [Squash non-direct-descendant merge](#squash-non-direct-descendant-merge)
  - Upstream repository to upstream repository: cherry-pick or merge (depends on the strategy taken)
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: cherry-pick
- [Subtree point-import](#subtree-point-imports)
  - Upstream repository to upstream repository: cherry-pick
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: merge
- [Subtree direct-descendant merge](#subtree-direct-descendant-merge)
  - Upstream repository to upstream repository: merge
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: merge
- [Subtree non-direct-descendant merge](#subtree-non-direct-descendant-merge)
  - Upstream repository to upstream repository: cherry-pick or merge (depends on the strategy taken)
  - Upstream repository to [yugabyte/yugabyte-db][repo-yugabyte-db]: merge

##### Review cherry-picks

For cherry-picks, compare the original commit's patch with the cherry-pick commit's patch.
One way to do it is `$difftool <(git show <commit_being_cherry-picked>) <(git show <cherry-picked_commit>)`.
In case this is a cross-repository cherry-pick, `$difftool <(git -C /path/to/upstream_repo show <commit_being_cherry-picked>) <(git -C /path/to/yugabyte_repo show <cherry-picked_commit>)`.
In case this is a cross-repository cherry-pick of multiple commits and you do not have access to individual cherry-picks, `$difftool <(git -C /path/to/upstream_repo diff <cherry-picks_commit_range>) <(git -C /path/to/yugabyte_repo show ...)`.
There is also a tool [`analyze_cherry_pick.py`](https://gist.github.com/hari90/b65159b6811786023e0f0ea2af448f4a) authored by Hari you can try out.

First, check the [commit message](#cherry-pick-commit-message).

Then, check the code and resolution notes.
Line numbers and context may differ.
Pay attention to things like...

- ...added code being placed in the wrong context (you can expand context of `git show` using `-U50`, for example).
- ...mismatching whitespace or newlines between the two patches.
- ...nontrivial differences between the patches that are not explained adequately in the resolution notes.

It may be difficult to compare large [cross-repository patches](#cross-repository-cherry-pick) for direct-descendant or non-direct-descendant squash merges.
In that case, a throw-away merge commit can be created for review purposes.
If the author followed the [cross-repository cherry-pick steps](#cross-repository-cherry-pick), the author may be able to skip some of the early steps here as they are redundant:

1. Check out the [yugabyte/yugabyte-db][repo-yugabyte-db] base commit of this squash merge.
1. On the upstream repository commit before the upstream merge (which should equal the commit recorded in [`upstream_repositories.csv`][upstream-repositories-csv] in [yugabyte/yugabyte-db][repo-yugabyte-db]), sync the content of [yugabyte/yugabyte-db][repo-yugabyte-db]'s upstream repository directory here.
   Use `git add -u` to add all those changes (not the new files), and commit them (commit message does not matter since this is not an official commit).
1. Check out the [yugabyte/yugabyte-db][repo-yugabyte-db] squash merge commit.
1. In the upstream repository, `git merge` the upstream merge commit.
   Since the merge was already previously done, just sync the new content of [yugabyte/yugabyte-db][repo-yugabyte-db]'s upstream repository directory here.
1. [Review](#review-merges) the code and resolution of this merge commit, ignoring the commit message since this is a throw-away commit.

On top of that, make sure that the Git metadata is proper where it matters.

- For the upstream repository, there should only be the cherry-picked commits.
  If there are any other commits, there should be a good reason for them, and the commit titles should start with `YB:`.
  Merge conflicts (including logical ones) should generally be resolved and amended into the same commit being cherry-picked.
  The [Git author information](#git-author-information) should be preserved for cherry-picked commits.
- For [yugabyte/yugabyte-db][repo-yugabyte-db], only the squash embedding strategy uses cherry-picks.
  - If it's a single point-import, [Git author information](#git-author-information) should be preserved.
  - If it's multiple point-imports, the [Git author](#git-author-information) should be the person executing the point-imports.
  - If it's a direct-descendant or non-direct-descendant merge, the [Git author](#git-author-information) should be `YugaBot <yugabot@users.noreply.github.com>`.

##### Review merges

For merges, it is best to get an actual merge commit.
Phorge squashes any Git structure into a single patch, so the merge commit should be obtained through a separate medium such as a GitHub fork.

First, check the [commit message](#merge-commit-message).

Then, use `git show --diff-merges=dense-combined` (equivalent to just `git show`) on the merge commit to see the main resolutions.
Be aware that this doesn't show resolutions where one side of the merge was taken entirely.

On top of that, make sure that the Git metadata is proper where it matters.

- For the upstream repository, there should only be a single merge commit.
  The person executing the merge should be the [Git author](#git-author-information) of the merge commit.
- For [yugabyte/yugabyte-db][repo-yugabyte-db], only the subtree embedding strategy uses merges.
  The person executing the merge should be the [Git author](#git-author-information) of the subtree merge commit.
  On top of that, there should be a single commit to update [`upstream_repositories.csv`][upstream-repositories-csv].
  There should be no other commits besides the subtree ones.
  Make sure the subtree merge commit parent ordering is optimal: `git blame` should show more upstream commits compared to the reverse parent ordering, and if either ordering is equivalent, non-reversed parent ordering should be preferred.

##### Another way to review the merge

A different way to review the code and resolution is to do the entire merge yourself.
It takes longer but results in a more solid review.
You can use the resolution notes of the author as a cheat sheet to speed up the process.
This also helps make sure the author made clear resolution notes and covered explaining the necessary resolutions.

#### File-specific advice

- [`upstream_repositories.csv`][upstream-repositories-csv]:
  Make sure the commit hash is updated properly.
- `src/postgres/doc`:
  In general, changes here do not matter since this documentation is not built by YugabyteDB.
  The only place that really matters is `src/postgres/doc/src/sgml/ref` where some text is used by the `\h` command.
- Regression tests:
  There are various locations for regression tests, such as `src/postgres/src/test/regress`, `src/postgres/src/test/modules/dummy_seclabel`, `src/postgres/contrib/cube`, `src/postgres/third-party-extensions/pg_hint_plan`, and `src/postgres/yb-extensions/yb_xcluster_ddl_replication`.
  There are also some different types, such as the isolation regress tests in `src/postgres/src/test/isolation`.
  Any changes to these tests should be ported to `yb.port.` equivalents.
  (Note that, at the time of writing, pg_hint_plan is the one exception to this: YB changes are made on the original test directly.)
  See [build and test](../build-and-test#ysql-regress-tests).

[repo-postgres]: https://github.com/yugabyte/postgres
[repo-thirdparty]: https://github.com/yugabyte/yugabyte-db-thirdparty
[repo-yugabyte-db]: https://github.com/yugabyte/yugabyte-db
[upstream-repositories-csv]: https://github.com/yugabyte/yugabyte-db/blob/master/src/lint/upstream_repositories.csv
