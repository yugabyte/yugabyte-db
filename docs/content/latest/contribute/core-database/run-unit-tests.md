---
title: Testing the source code
linkTitle: Running the Tests
description: Running the Tests
image: /images/section_icons/index/quick_start.png
headcontent: Running the tests.
type: page
menu:
  latest:
    identifier: contribute-db-core-test
    parent: contribute-db-core
    weight: 2913
isTocNested: true
showAsideToc: true
---

## Running the C++ tests

### Run all tests

To run all the C++ tests you can use following command:

```bash
./yb_build.sh release --ctest
```

If you omit release argument, it will run java tests against debug YugaByte DB build.

### Run specific tests

To run a specific test, for example the `util_monotime-test` test, you can run the following command:

```bash
./yb_build.sh release --cxx-test util_monotime-test
```

To run a specific sub-test, for example the `TestMonoTime.TestCondition` sub-test in `util_monotime-test`, you can run the following command:

```bash
./yb_build.sh release --cxx-test util_monotime-test --gtest_filter TestMonoTime.TestCondition
```

## Running the Java tests

### Run all tests

Given that you've already built C++ and Java code you can run Java tests using following command:

```bash
./yb_build.sh release --scb --sj --java-tests
```

If you omit release argument, it will run java tests against debug YugaByte build, so you should then either build debug binaries with `./yb_build.sh` or omit `--scb` and then it will build debug binaries automatically.


### Run specific tests

To run specific test:

```bash
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file use the # syntax, for example:

```bash
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient#testClientCreateDestroy
```

### Viewing log outputs

You can find Java tests output in corresponding directory (you might need to change yb-client to respective Java tests module):

```bash
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

