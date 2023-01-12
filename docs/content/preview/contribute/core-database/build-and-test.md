---
title: Build and test
headerTitle: Build and test
linkTitle: Build and test
description: Build and test
image: /images/section_icons/index/quick_start.png
headcontent: Building and testing with yb_build.sh.
menu:
  preview:
    identifier: build-and-test
    parent: core-database
    weight: 2913
type: docs
---

`yb_build.sh` is a bash script for both building and testing.
Some flags can turn off certain parts of build, and other flags can decide which tests to run.
In general, they come down to these components:

- build:
  - C++: C++ code in `src/yb/` and `ent/src/yb` for `yb-master`, `yb-tserver`, `yb-admin`, test binaries, etc.
  - postgres: C code in `src/postgres/` for `postgres`, `ysql_dump`, `libyb_pgbackend`, `libpq`, etc.
  - Java: Java code in `java/` for CDC, Java tests, drivers, etc.
  - `initdb`: pre-created initial system catalog snapshot for YSQL
- test:
  - C++ tests
  - Java tests

For a full list of options, run `yb_build.sh -h`.
What follows are some of the common options to pay attention to.

## Build

### Targets

By default, all items are built.
You can specify a target to narrow down what to build.
See `./yb_build.sh -h` for the supported targets.
Besides that, certain flags may skip building some items.
For example, specifying a flag to run a C++ test skips the Java build (but running a Java test won't skip the C++ build).

Although there is some intelligence to avoid rebuilding parts, it is incomplete.
For example, the postgres build uses a build stamp calculated from a git commit and working changes; if there is a mismatch, it reruns postgres configure and make.
The Java build always runs the full build.
Use the `--skip-cxx-build`/`--scb` and `--skip-java-build`/`--sj` flags to reduce incremental build times.

`initdb` is a special case because it is only built if it hasn't been built before, but it isn't rebuilt again unless the `reinitdb` target is specified.
You should run `reinitdb` in case the initial system catalog in the source code is different since the last time `initdb` was run, unless you intentionally want to use the old initial system catalog (for example, to test upgrades).

### Build generator

CMake is used.

### Build tool

By default, `ninja` is used, but `make` is also supported.
Specify the build tool using the `--make` and `--ninja` flags.

### Build type

The following build types are available:

- debug (the default)
- release
- fastdebug: fewer compiler optimizations and a few other tweaks on debug
- asan: address sanitizer
- tsan: thread sanitizer

### Compiler

By default, `clang` is used, but `gcc` is also supported.

Specify the compiler using the `--gcc<version_number>` and `--clang<version_number>` flags.
The specific versions supported can be found in `./build-support/third-party-archives.yml`, which details the configurations regularly tested in-house.

### Third-party

By default, third-party libraries are pre-built into archives, and those archives are downloaded to be used during build.
Incremental builds currently do not detect when the third-party archive URL has been updated, so when that happens, you should run a `--clean` build to use the new third-party archive.
You can also build the third-party archives locally using `--no-download-thirdparty` or `--ndltp`.

## Test

The following examples use the `release` build type, but any build type can be specified, provided that build type is not disabled for that test.
For example, several tests are disabled for the `tsan` build type.

### C++ tests

By default, the `yb_build.sh` commands to run C++ tests will first build those tests.
Build can be avoided using `--skip-cxx-build` or `--scb`.

#### Run all tests

To run all the C++ tests, run the following command:

```sh
./yb_build.sh release --ctest
```

#### Run specific tests

To run a specific test, for example the `util_monotime-test` test, run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test
```

To run a specific sub-test, for example the `TestMonoTime.TestCondition` sub-test in `util_monotime-test`, run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test --gtest_filter TestMonoTime.TestCondition
```

`--gtest_filter` supports globbing.
For example, `TestMonoTime.TestTime*` runs both `TestMonoTime.TestTimeVal` and `TestMonoTime.TestTimeSpec`.
Make sure to escape or quote the `*` if your shell interprets it as a glob character.

### Java tests

By default, the `yb_build.sh` commands to run Java tests will first build key C++ targets and do the full Java build.
Build can be avoided using `--skip-cxx-build`/`--scb` and `--skip-java-build`/`--sj`.

#### Run all tests

To run all the Java tests, use the following command:

```sh
./yb_build.sh release --java-tests
```

#### Run specific tests

To run a specific test:

```sh
./yb_build.sh release --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file, use the # syntax.
For example:

```sh
./yb_build.sh release --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```

`--scb` and `--sj` are recommended in case the C++ and Java code was already built beforehand.

### Run tests multiple times

`yb_build.sh` has a built-in capability of running tests multiple times with parallelism.

- `--num_repetitions`/`-n`: number of times to run the test(s)
- `--test-parallelism`/`--tp`: number of instances of the test to run in parallel

Note that a high parallelism could result in failures if system resources are overused.

### Test frameworks

#### C++ (external) mini cluster

C++ tests are located in `src/yb` or `ent/src/yb` and end in `test.cc`.

Many C++ tests use external mini cluster or mini cluster to create clusters.
A key difference is that external mini clusters create separate master, tserver, and postgres processes (as in real life), while mini clusters create masters and tservers within the same process and optionally spawn a separate postgres process.
Use external mini clusters instead of mini clusters unless you have a specific reason to do otherwise, such as:

- you need access to catalog manager internals
- there is little to no YSQL usage and speed is more important

#### YSQL regress tests

YSQL Java tests are located in `java/yb-pgsql/src/test/java/org/yb/pgsql/`.

Some of those tests, named `TestPgRegress*`, use the PostgreSQL regress test framework (see `src/postgres/src/test/regress`).
They each correspond to a schedule (for example, `java/yb-pgsql/src/test/java/org/yb/pgsql/TestPgRegressArrays.java` references `src/postgres/src/test/regress/yb_arrays_schedule`) that is run by our modified version of `pg_regress`.
Each schedule lists a serial order of test files to run.
The same cluster is used for the whole schedule, but a new SQL connection is made for each test file.

The test framework does the following:

1. Copies test files from the `src` directory to the `build` directory (this is technically done as part of build).
1. Copies test files to a temporary `test` directory.
1. In the `test` directory, generates `sql`/`expected` from `input`/`output`
1. In the `test` directory, generates `results` by running tests in `sql`.
1. Copies back `results` from the `test` directory to the `build` directory.
1. Deletes the `test` directory.

The files in `results` are compared with those in `expected` to determine pass/fail.

{{< tip title="Tips" >}}

- To quickly run a specific SQL file, create a dummy Java file and dummy schedule with that one test in it.
- Use the following naming convention (some older files haven't adopted it yet, but should):
  - `sql/foo.sql`: Unchanged from original PostgreSQL code.
  - `sql/yb_foo.sql`: Completely new file (for example, with new features).
  - `sql/yb_pg_foo.sql`: Modified version of original PostgreSQL foo.sql (for example, with compatibility edits).
    The goal here is to reduce the difference between `foo.sql` and `yb_pg_foo.sql`, when possible.

{{< /tip >}}

### Test flags

The general hierarchy of flags is as follows:

1. Test framework: the framework may specify some default flags.
1. Test superclass: any parent classes of the test class can set flags.
1. Test subclass: child class flags should take precedence over parent class flags.
1. Test: a test itself may set flags.
1. User: the user running the test could add flags.

There may be some areas where the order of precedence is not followed: help fixing this is welcome.

Some of the ways in which you can specify flags to tests (uppercase are environment variables) are as follows:

- `YB_EXTRA_DAEMON_FLAGS`, `--extra-daemon-flags`, `--extra-daemon-args`: pass flags to master and tserver processes.
  (This does not work on mini cluster.)
- `YB_EXTRA_MASTER_FLAGS`: pass flags to master processes.
  (Does not work on mini cluster.)
- `YB_EXTRA_TSERVER_FLAGS`: pass flags to tserver processes.
  (Does not work on mini cluster.)
- `--test-args`: for C++ tests, pass flags to the test process.
  (For mini cluster, it also affects masters/tservers since they share the same process.)
- `YB_EXTRA_MVN_OPTIONS_IN_TESTS`, `--java-test-args`: for Java tests, pass flags in the form of Maven system parameters.

Here are some examples:

```sh
YB_EXTRA_DAEMON_FLAGS="--log_ysql_catalog_versions=true" \
  YB_EXTRA_MASTER_FLAGS="--vmodule=master_heartbeat_service=2" \
  YB_EXTRA_TSERVER_FLAGS="--vmodule=heartbeater=2" \
  ./yb_build.sh \
  --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.DBCatalogVersion
```

```sh
./yb_build.sh \
  --cxx-test integration-tests_master_failover-itest \
  --gtest_filter MasterFailoverTest.DereferenceTasks \
  --test-args "--vmodule=master_failover-itest=1,catalog_manager=4"
```

```sh
export YB_EXTRA_MVN_OPTIONS_IN_TESTS="-Dstyle.color=always"
./yb_build.sh \
  --java-test TestPgRegressPgMisc \
  --java-test-args "-Dyb.javatest.keepdata=true"
```
