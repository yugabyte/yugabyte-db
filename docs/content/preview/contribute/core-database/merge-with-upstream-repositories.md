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
The pros and cons are detailed in a different section.
TODO(jason): reference that section.

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
1. On latest `yugabyte/postgres` repo `yb-pg<version>` branch, **do `git cherry-pick -x <commit>` for each commit being imported**.
   Notice the `-x` to record the commit hash being cherry-picked.
   For any merge conflicts, resolve and amend them to that same commit, describing resolutions within the commit messages themselves.
   This includes logical merge conflicts which can be found via compilation failure or test failure.
   See TESTING.
   TODO(jason): where is TESTING?
   At the end, you should be n commits ahead of `yb-pg<version>,` where n is the number of commits you are point-importing.
   Make a GitHub PR for this for review.
1. On `yugabyte/yugabyte-db` repo, import the commits that are part of the `yugabyte/postgres` repo PR.
   This is not as straightforward as a cherry-pick since it is across different repositories: see [cross-repository patch][cross-repo-patch] for advice.
   TODO(jason): another alternative is to apply the giant patch to yugabyte-db.
   For any merge-related changes, keep track of resolution details in some temporary place.
   Unlike as in `yugabyte/postgres`, the commit structure in `yugabyte/yugabyte-db` does not matter since these changes will eventually be squashed.
   See MERGE.
   TODO(jason): where is MERGE?
   Once all commits are imported, update `src/lint/upstream_repositories.csv`, and create a Phorge revision.
   The Phorge summary should have the upstream postgres commit hashes and their commit messages.
   Add to the summary the merge resolution details that were previously recorded.
1. Pass review for both the `yugabyte/postgres` imports and the `yugabyte/yugabyte-db` revision.
   Besides general merge review, here are things reviewers should watch out for in the `yugabyte/postgres` review:

   - Was `-x` used for each cherry-pick?
   - Was the commit author metadata preserved for each cherry-pick?
   - Was the commit message preserved for each cherry-pick?
   - Are there exactly n commits, and are they branched off the latest `yb-pg<version>` commit?

1. Land the `yugabyte/yugabyte-db` revision.
   **If there is a merge conflict on `src/lint/upstream_repositories.csv`, then redo the whole process.**
   It means someone else updated `yb-pg<version>` branch, so `yugabyte/postgres` cherry-picks need to be rebased on latest `yb-pg<version>`, compilation/testing needs to be re-done, and since the commit hashes change, `src/lint/upstream_repositories.csv` needs a new commit hash.
   Plus, it's safest to re-run tests on `yugabyte/yugabyte-db` after all this adjustment.
1. Land the `yugabyte/postgres` commits directly onto `yb-pg<version>`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv`, and if landing that change passes, then no one should have touched `yb-pg<version>` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg<version>`.**

#### Squash direct-descendant merge

A direct-descendant merge for PostgreSQL is typically a minor version upgrade.
An example is PG 15.2 to 15.12 as done in [`yugabyte/postgres` 12398eddbd531080239c350528da38268ac0fa0e](https://github.com/yugabyte/postgres/commit/12398eddbd531080239c350528da38268ac0fa0e) and [`yugabyte/yugabyte-db` e99df6f4d97e5c002d3c4b89c74a778ad0ac0932](https://github.com/yugabyte/yugabyte-db/commit/e99df6f4d97e5c002d3c4b89c74a778ad0ac0932).

1. On latest `yugabyte/postgres` repo, `git merge <commit>`.
   Record merge resolutions into the merge commit message.
   There should only be the merge commit and no other extraneous commits on top of it.
   Make a GitHub PR for this for review.
1. Apply those changes to the `yugabyte/yugabyte-db` repo.
   See [cross-repository patch][cross-repo-patch] for details.
   Update `src/lint/upstream_repositories.csv`, and create a Phorge revision listing all the merge conflict details.
   Ensure that the [author information][git-author-information] for the latest commit of this Phorge revision is `YugaBot <yugabot@users.noreply.github.com>`.
1. Pass review for both `yugabyte/postgres` GitHub PR and `yugabyte/yugabyte-db` Phorge revision.
   If `yugabyte/postgres` needs changes, then commit hashes change, so `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv` must be updated, and re-test on `yugabyte/yugabyte-db` should be considered.
1. Land the `yugabyte/yugabyte-db` revision.
   **If there is a merge conflict on `src/lint/upstream_repositories.csv`, then redo the whole process.**
   It means someone else updated `yb-pg<version>` branch, so `yugabyte/postgres` merge need to be redone on latest `yb-pg<version>`, compilation/testing needs to be re-done, and since the commit hashes change, `src/lint/upstream_repositories.csv` needs a new commit hash.
   Plus, it's safest to re-run tests on `yugabyte/yugabyte-db` after all this adjustment.
1. Land the `yugabyte/postgres` merge directly onto `yb-pg<version>`.
   **Do not use the GitHub PR UI to merge because that changes commit hashes.**
   This should not run into any conflicts; there should be no need for force push.
   This is because any conflicts are expected to be encountered on `yugabyte/yugabyte-db` `src/lint/upstream_repositories.csv`, and if landing that change passes, then no one should have touched `yb-pg<version>` concurrently.
   **Make sure the commit hashes have not changed when landing to `yb-pg<version>`.**

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
1. On `yugabyte/postgres` repo, identify the point-imports or custom changes on `yb-pg15` that need to be carried over to PG 18.1.
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
   The procedure here is very much like the first two steps of [doing point-imports](#squash-point-imports).
1. Do the same steps as in [direct-descendant merge](#squash-direct-descendant-merge), starting from "Apply those changes to the `yugabyte/yugabyte-db` repo."
   A difference is that this merge may be so much larger that the initial merge is incomplete and potentially based off an old commit on `yugabyte/yugabyte-db`.
   This initial merge commit and further development to close the gaps can be done in a separate `pg18` branch on `yugabyte/yugabyte-db`.
   This is what was done for the PG 15 merge, from `yugabyte/yugabyte-db` [55782d561e55ef972f2470a4ae887dd791bb4a97](https://github.com/yugabyte/yugabyte-db/commit/55782d561e55ef972f2470a4ae887dd791bb4a97) to [eac5ed5d186b492702a0b546bf82ed162da506b0]((https://github.com/yugabyte/yugabyte-db/commit/eac5ed5d186b492702a0b546bf82ed162da506b0).

### Embedded via subtree

The steps that follow will use pgaudit as an example.
At the time of writing, pgaudit uses the subtree embedding strategy.

#### Subtree point-imports

#### Subtree direct-descendant merge

#### Subtree non-direct-descendant merge

## General advice

MERGE:
- port regress tests

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
[cross-repo-patch]: TODO(jason)
[git-author-information]: TODO(jason)
