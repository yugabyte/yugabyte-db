## Making changes and pushing to upstream
Never operate on master branch directly. Always create a new local branch and work on that.
Pushing the branch to yugabyte/yugabyte-db repository is not allowed. If you are on a personal fork, you can push to that.
arc and phorge are used to review, run lab tests, and merge changes. This should ONLY be done by the human.

Avoid using non-ASCII characters in files and commit messages.
There may be some exceptions where appropriate such as `collate.icu.utf8.sql` and `jsonpath_encoding.out`.

## Build System

The primary build entry point is `yb_build.sh` at the repository root.

Reuse existing build compiler/type if available (see `build/latest` symlink); default to `release` otherwise.

Add these `yb_build.sh` options to reduce build time:
- Specify only the cmake targets you need (for example, `daemons initdb`).
- Skip java build (`--sj`) unless you have to run java tests.
- Skip pg_parquet build (`--skip-pg-parquet`) unless you need it.
- Skip odyssey build (`--no-odyssey`) unless you need it.
- Skip YBC build (`--no-ybc`) unless you need it.

Pitfalls when doing incremental build:
- `initdb` cmake target in conjunction with `yb_build.sh` test options may not build `initdb`, so in that case, do them one by one.
- Forgetting the `reinitdb` cmake target after changes to the system catalog since last build may cause failures.
- Forgetting `--clean` after changes to third-party since last build may cause failures.

```bash
./yb_build.sh release initdb
```

### Common Build Commands

```bash
./yb_build.sh release daemons initdb --sj --skip-pg-parquet --no-odyssey --no-ybc
```

### C++ Tests

```bash
./yb_build.sh release --cxx-test tablet-test

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter TestLoadBalancerPreferredLeader.TestBalancingMultiPriorityWildcardLeaderPreference

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test --gtest_filter "*Wildcard*"

./yb_build.sh release --cxx-test cluster_balance_preferred_leader-test -n 10
```

### Java Tests

```bash
./yb_build.sh release --java-test org.yb.client.TestYBClient

./yb_build.sh release --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```
