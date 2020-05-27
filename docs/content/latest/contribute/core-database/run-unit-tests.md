---
title: Run the tests
headerTitle: Run the tests
linkTitle: Run the tests
description: Run the tests
image: /images/section_icons/index/quick_start.png
headcontent: Run the tests.
type: page
menu:
  latest:
    identifier: run-unit-tests
    parent: core-database
    weight: 2913
isTocNested: true
showAsideToc: true
---


## Running the C++ tests

### Run all tests

To run all the C++ tests you can use following command:

```sh
./yb_build.sh release --ctest
```

If you omit release argument, it will run java tests against debug YugabyteDB build.

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

Given that you've already built C++ and Java code you can run Java tests using following command:

```sh
./yb_build.sh release --scb --sj --java-tests
```

If you omit release argument, it will run java tests against debug YugabyteDB build, so you should then either build debug binaries with `./yb_build.sh` or omit `--scb` and then it will build debug binaries automatically.

### Run specific tests

To run specific test:

```sh
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file use the # syntax, for example:

```sh
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient#testClientCreateDestroy
```

### Viewing log outputs

You can find Java tests output in corresponding directory (you might need to change yb-client to respective Java tests module):

```sh
$ ls -1 java/yb-client/target/surefire-reports/
TEST-org.yb.client.TestYBClient.xml
org.yb.client.TestYBClient-output.txt
org.yb.client.TestYBClient.testAffinitizedLeaders.stderr.txt
org.yb.client.TestYBClient.testAffinitizedLeaders.stdout.txt
…
org.yb.client.TestYBClient.testWaitForLoadBalance.stderr.txt
org.yb.client.TestYBClient.testWaitForLoadBalance.stdout.txt
org.yb.client.TestYBClient.txt
```

{{< note title="Note" >}}
The YB logs are contained in the output file now.
{{< /note >}}

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
If a build fails to pick up the changes and fails to copy them, you can remove `build/latest/postgres_build` to force a recopy or copy them yourself manually.


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
