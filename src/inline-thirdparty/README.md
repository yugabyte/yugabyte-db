# inline-thirdparty

This is a directory where we copy some of the third-party header-only libraries, rather than adding
them to the yugabyte-db-thirdparty repo. We also only copy the relevant subdirectory of the upstream
repositories. Each library is copied in its own appropriately named directory, and each library's
directory is added separately to the list of include directories in CMakeLists.txt.

The list of inline third-party dependencies is specified in build-support/inline_thirdparty.yml.
Each dependency has the following fields:
- name: The name of the dependency.
- git_url: upstream git URL, typically github, either a YugabyteDB fork of the upstream repo, or
  the official repository.
- tag or commit: The git tag or commit to ise.
- src_dir: the directory within the upstream repository to copy.
- dest_dir: the target directory to copy the upstream repository to, relative to
  src/inline-thirdparty. This has to start with the dependency name as the first path component
  for clarity, but could be a deeper directory.

To update inline third-party dependencies, modify the tag or comimt in inline_thirdparty.yml,
commit the changes, and run the following command:

build-support/thirdparty_tool --sync-inline-thirdparty

This requires the absence of local changes in the git repository. This will create one or more
commits for each dependency. These commits should be submitted as a Phabricator diff for review,
tested in Jenkins, and then landed. The changes to inline-thirdparty should be pushed as
separate commits, not squashed together.
