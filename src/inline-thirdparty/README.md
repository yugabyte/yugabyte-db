# inline-thirdparty

## Overview

The src/inline-thirdparty directory is where we copy parts of the code of
certain third-party header-only libraries, rather than adding those libraries
to the yugabyte-db-thirdparty repo. This allows us to modify this code in place
during development, while avoiding the heavyweight third-party archive workflow
that we use for regular third-party dependencies. We only copy a single
relevant subdirectory of the upstream repository for each inline third-party
dependency, and we have a convenient tool to help with this. Each library is
copied in its own appropriately-named subdirectory of src/inline-thirdparty,
and each dependency's directory is added separately to the list of include
directories in the top-level CMakeLists.txt. To add a new directory there, look
for lines similar to the following:

include_directories("src/inline-thirdparty/fp16")
include_directories("src/inline-thirdparty/hnswlib")
include_directories("src/inline-thirdparty/simsimd")
include_directories("src/inline-thirdparty/usearch")

## Configuring the inline third-party dependencies

The list of inline third-party dependencies is specified in
build-support/inline_thirdparty.yml. Each dependency has the following fields:

- name: The all-lowercase name of the dependency. Used for directory naming.

- git_url: upstream git URL, typically GitHub. Either a YugabyteDB fork of the
upstream repo, or the official repository. This might switch from the
YugabyteDB fork to the upstream fork of the repo and vice versa, depending on
whether the most recent changes we rely on have been merged to an official
upstream branch, or if they are only present in the YugabyteDB fork.

- tag or commit: The git tag or commit to use.

- src_dir: the directory within the upstream repository to copy.

- dest_dir: the target directory to copy the upstream repository to, relative
to src/inline-thirdparty. This has to start with the dependency name as the
first path component for clarity, but could be a relative path consisting of
multiple components.

## Development workflow

The advantage of this setup is that we can modify the code of these "inline"
("header-only") third-party libraries in place during development. For example,
editing index_dense.hpp (part of Usearch) or hnswalg.h (part of Hnswlib) and
immediately getting results from testing.

Once the changes have been made, they should be submitted as PRs into
YugabyteDB forks of the respective libraries, reviewed and merged into the
appropriate YugabyteDB branches, which would be branched off of upstream
branches. Also PRs should be submitted to upstream repositories.

Some examples of how this workflow has unfolded in practice are below.

### Usearch

The upstream repository is at https://github.com/unum-cloud/usearch.
The YugabyteDB fork is at https://github.com/yugabyte/usearch.

Recently, as of the time of this writing, we created a branch 2.15.3-yb off of
the upstream commit 8fa309043a19d93473a12f4243126945c3b40373
( https://github.com/unum-cloud/usearch/commits/8fa309043a19d93473a12f4243126945c3b40373 )
which is actually a few commits ahead of what the Usearch tag v2.15.3 points to
( 9bc9936f6b3dc9c9bd5d70bfd887fb18e20795eb ), but there are no changes to the
include/usearch directory between those commits:

git diff 9bc9936f6b3dc9c9bd5d70bfd887fb18e20795eb 8fa309043a19d93473a12f4243126945c3b40373 -- ./include/usearch

On top of that, we added two additional commits to the 2.15.3-yb branch:

https://github.com/yugabyte/usearch/commit/3d37c7eb3eef8517ec4597b244d7edcf91c53170
https://github.com/yugabyte/usearch/commit/d7f8ad9d24af1962f110393bcef859f0efa71561

These commits were submitted to the upstream Usearch repository:

https://github.com/unum-cloud/usearch/pull/508

As of the time of this writing, the above PR has already been merged into the
upstream usearch repo, but we will describe the workflow that was followed
before the PR was merged. We created the tag v2.15.3-yb-2 on the 2.15.3-yb
branch of https://github.com/yugabyte/usearch and updated the entry in
inline_thirdparty.yml to the following:

```yaml
  - name: usearch
    git_url: https://github.com/yugabyte/usearch
    tag: v2.15.3-yb-2
    src_dir: include
    dest_dir: usearch
```

### Hnswlib

The upstream repository is at https://github.com/nmslib/hnswlib.
The YugabyteDB fork is at https://github.com/yugabyte/hnswlib.

We created a branch vc1b9b79a-yb-1 ("v" for "version") in our fork of Hnswlib
off of the upstream commit c1b9b79af3d10c6ee7b5d0afa1ce851ae975254c, and added
the following custom commit:

https://github.com/yugabyte/hnswlib/commit/57522f8e1b4184b0335f6ff40f46d8283cbc8dd7

We submitted this commit as a PR to the upstream Hnswlib:

https://github.com/nmslib/hnswlib/pull/594

We also created a tag vc1b9b79a-yb-1 in our vc1b9b79a-yb-1 branch, and updated
the entry in inline_thirdparty.yml as follows:

```yaml
  - name: hnswlib
    git_url: https://github.com/yugabyte/hnswlib
    tag: vc1b9b79a-yb-1
    src_dir: hnswlib
    dest_dir: hnswlib/hnswlib
```

## Landing the changes in the yugabyte-db repo

Once the changes to libraries have been merged into the appropriate YugabyteDB
forks, the tags have been created, and inline_thirdpary.yml has been updated
and committed locally, you can use the thirdparty_tool syntax below to create
local commits, one per dependency, that overwrite the code in the corresponding
subdirectory with the appropriate version of the dependency. We would suggest
reverting any local changes to src/inline-thirdparty prior to doing this.

build-support/thirdparty_tool --sync-inline-thirdparty

These commits should be submitted as a Phabricator diff for review, tested in
Jenkins, and then landed. The changes to inline-thirdparty should be pushed as
separate commits, not squashed together.

Here is an example corresponding to the changes described in the preceding
section.

```
commit 82bb5a94533b038efe9bb25fedb472b981a03b4c (tag: 2.25.0.0-b184)
CommitDate: Tue Oct 22 09:39:16 2024 -0700

    Automatic commit by thirdparty_tool: update hnswlib to tag vc1b9b79a-yb-1.

    Used commit of the hnswlib repository: https://github.com/yugabyte/hnswlib/commits/57522f8e1b4184b0335f6ff40f46d8283cbc8dd7

commit a72678814289abba64558fa917416b9034ae526a
CommitDate: Tue Oct 22 09:39:14 2024 -0700

    Automatic commit by thirdparty_tool: update usearch to tag v2.15.3-yb-2.

    Used commit of the usearch repository: https://github.com/yugabyte/usearch/commits/d7f8ad9d24af1962f110393bcef859f0efa71561

commit 1b2db39dd57fa53647c285649dda85fbe167a08d
CommitDate: Tue Oct 22 09:39:02 2024 -0700

    [#24465] Fix Usearch / Hnswlib precision discrepancy
    . . .
    Differential Revision: https://phorge.dev.yugabyte.com/D38977
```

In this example, the diff https://phorge.dev.yugabyte.com/D38977 was tested
in Jenkins with the two extra commits included, but landed without the two
extra commits, and then those commits were added back with thirdparty_tool
and pushed into the master branch of the yugabyte-db repo manually. This is the
suggested workflow that we recommend.
