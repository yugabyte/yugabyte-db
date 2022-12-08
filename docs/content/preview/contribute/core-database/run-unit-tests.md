---
title: Run the tests
headerTitle: Run the tests
linkTitle: Run the tests
description: Run the tests
image: /images/section_icons/index/quick_start.png
headcontent: Run the tests.
menu:
  preview:
    identifier: run-unit-tests
    parent: core-database
    weight: 2913
type: docs
---


## Running the C++ tests

### Run all tests

To run all the C++ tests, you can use following command:

```sh
./yb_build.sh release --ctest
```

If you omit the release argument, it will use the debug build type.

### Run specific tests

To run a specific test, for example the `util_monotime-test` test, you can run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test
```

To run a specific sub-test, for example the `TestMonoTime.TestCondition` sub-test in `util_monotime-test`, you can run the following command:

```sh
./yb_build.sh release --cxx-test util_monotime-test --gtest_filter TestMonoTime.TestCondition
```

## Running the Java tests

### Run all tests

After you've built the C++ and Java code, run the Java tests using the following command:

```sh
./yb_build.sh release --scb --sj --java-tests
```

If you omit the release argument, it will run Java tests against the debug build, so you should either omit `--scb` or first build debug binaries by running `./yb_build.sh` with no test options.

### Run specific tests

To run specific test:

```sh
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file use the # syntax, for example:

```sh
./yb_build.sh release --scb --sj --java-test 'org.yb.client.TestYBClient#testClientCreateDestroy'
```

### Viewing log outputs

You can find Java test output in files typically matching this pattern: `java/*/target/surefire-reports_*/*-output.txt`.

Test output includes the log location. For example:

```
[postprocess_test_result.py:183] 2022-10-28 10:49:43,664 INFO: Log path: /path/to/repo/java/yb-pgsql/target/surefire-reports_org.yb.pgsql.TestIndexBackfill__insertsWhileCreatingIndex/org.yb.pgsql.TestIndexBackfill-output.txt
```

### YSQL Java tests

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
