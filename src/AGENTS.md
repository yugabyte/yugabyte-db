## Build System

The primary build entry point is `yb_build.sh` at the repository root.

Use release build by default. Only use debug builds when explicitly asked to do so.
Skip java build (`--sj`) unless you have to run java tests.

The first time you run a test, you will need to run initdb beforehand:

```bash
./yb_build.sh release initdb
```

### Common Build Commands

```bash
./yb_build.sh release --sj
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
