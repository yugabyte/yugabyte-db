---
title: Build and test
headerTitle: Build and test
linkTitle: Build and test
description: Build and test
image: /images/section_icons/index/quick_start.png
headcontent: Build and test.
menu:
  preview:
    identifier: build-and-test
    parent: core-database
    weight: 2913
type: docs
---


# `yb_build.sh`

`yb_build.sh` is one bash script for both building and testing.
Some flags can turn off certain parts of build, and other flags can decide which tests to run.
In general, they come down to these components:

- build:
  - C++: C++ code for `yb-master` and `yb-tserver` binaries
  - postgres: C code for `postgres` binary and linked libraries for `yb-master` and `yb-tserver`
  - Java: CDC, Java tests, drivers, etc.
  - C++ tests
  - `initdb`: precreated initial system catalog snapshot for YSQL
- test:
  - C++ tests
  - Java tests

## Build

### Targets

By default, all items are built.
A target can be specified to narrow down what to build.
See `./yb_build.sh --help` for the supported targets.
Besides that, certain flags may skip build of some items.
For example, specifying a flag to run a C++ test will skip Java build (but running a Java test will not skip C++ build).

Although there is some intelligence to avoid rebuilding parts, it is incomplete.
For example, postgres build uses a build stamp calculated off of a git commit and working changes, and on a mismatch, it will rerun postgres configure and make.
Java is worse in that it runs all the build even if there are no changes since the last run.
Flags `--skip-cxx-build`/`--scb` and `--skip-java-build`/`--sj` can be useful to reduce incremental build times.

`initdb` is a special case because it will only be built if it wasn't built before, but it won't be rebuilt again unless the `reinitdb` target is specified.
It should be run in case the initial system catalog in the source code is different since the last time `initdb` was run, unless you intentionally want to use the old initial system catalog (for example, to test upgrades).

### Build generator

CMake is used.

If there are any changes to CMake files since the last build, the next incremental build may throw a compilation error such as

```
FAILED: build.ninja
```

In that case, run the build with `--force-run-cmake`/`--frcm`.

### Build tool

By default, `ninja` is used, but `make` is also supported.
It can be specified using the `--make` and `--ninja` flags.

### Build type

Several build types are available:

- release
- fastdebug: less compiler optimizations and a few other tweaks on debug
- debug
- asan: address sanitizer
- tsan: thread sanitizer

By default, `debug` is used.

### Compiler

By default, `clang` is used, but `gcc` is also supported.

It can be specified using the `--gcc<version_number>` and `--clang<version_number>` flags.
For example, `--gcc11` or `--clang15`.
The specific versions supported can be found in `./build-support/third-party-archives.yml`, which details the configurations regularly tested in-house.

### Thirdparty

By default, thirdparty libraries are prebuilt into archives, and those archives are downloaded to be used during build.
Incremental builds currently do not detect when the thirdparty archive URL has been updated, so when that happens, you should manually run a `--clean` build to use the new thirdparty.

## Test

The following examples will use the `release` build type, but any build type can be specified, provided that build type is not disabled for that test.
For example, several tests are disabled for the `tsan` build type.

### C++ tests

#### Run all tests

To run all the C++ tests, you can use the following command:

```sh
./yb_build.sh release --ctest
```

#### Run specific tests

To run a specific test, for example the `util_monotime-test` test, you can run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test
```

To run a specific sub-test, for example the `TestMonoTime.TestCondition` sub-test in `util_monotime-test`, you can run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test --gtest_filter TestMonoTime.TestCondition
```

`--gtest_filter` supports globbing.
For example, `TestMonoTime.TestTime*` runs both `TestMonoTime.TestTimeVal` and `TestMonoTime.TestTimeSpec`.
Make sure to escape or quote the `*` if your shell interprets it as a glob character.

### Java tests

#### Run all tests

To run all the Java tests, you can use the following command:

```sh
./yb_build.sh release --java-tests
```

#### Run specific tests

To run a specific test:

```sh
./yb_build.sh release --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file use the # syntax, for example:

```sh
./yb_build.sh release --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```

`--scb` and `--sj` are recommended in case the C++ and Java code was already built beforehand.

#### YSQL Java tests

YSQL java tests are in `java/yb-pgsql/src/test/java/org/yb/pgsql/`.  They can be run as:

```bash
./yb_build.sh --java-test org.yb.pgsql.TestPgTruncate
```

Some of those tests, `TestPgRegress*`, use the PostgreSQL regress test framework: `src/postgres/src/test/regress`.
They should each correspond to a schedule (e.g. `java/yb-pgsql/src/test/java/org/yb/pgsql/TestPgRegressArrays.java` references `src/postgres/src/test/regress/yb_arrays_schedule`)
that is run by our modified version of `pg_regress`.

Each schedule has a serial order of files to run.  For example, the `yb_arrays_schedule` will first run `build/latest/postgres_build/src/test/regress/sql/yb_pg_int8.sql`
and output to `build/latest/postgres_build/src/test/regress/results/yb_pg_int8.out` because the first `test:` line in `yb_arrays_schedule` is `test: yb_pg_int8`.
This will be compared with `build/latest/postgres_build/src/test/regress/expected/yb_pg_int8.out` for pass/fail.
Note the `build/latest/postgres_build` prefix.  The source files (`src/postgres/src/test/regress/sql/foo.sql`) get copied there (`build/latest/postgres_build/src/test/regress/src/foo.sql`).

{{< tip title="Tips" >}}

   - If you want to quickly run specific sql files, you can create a dummy java file and dummy schedule with that one test in it.
   - Use the naming convention (some older files haven't adopted it yet, but should):
     - `src/postgres/src/test/regress/sql/foo.sql`: unchanged from original PostgreSQL code
     - `src/postgres/src/test/regress/sql/yb_foo.sql`: completely new file (for example, with new features)
     - `src/postgres/src/test/regress/sql/yb_pg_foo.sql`: modified version of original PostgreSQL foo.sql (e.g. for compatibility edits)
     - The goal here is to reduce the difference between `foo.sql` and `yb_pg_foo.sql`, when possible.
   - When creating new `yb_pg_foo.{out,sql}` files and adding them to one of our schedules, sort them into the schedule using `src/postgres/src/test/regress/serial_schedule` as reference.
     Schedules `parallel_schedule` and `serial_schedule` should be untouched as they are from original PostgreSQL code.

{{< /tip >}}

### Run tests multiple times

`yb_build.sh` has a built-in capability of running tests multiple times with parallelism.

- `--num_repetitions`/`-n`: number of times to run the test(s)
- `--test-parallelism`/`--tp`: number of instances of the test to run in parallel

Note that a high parallelism could result in failures if system resources are overused.
