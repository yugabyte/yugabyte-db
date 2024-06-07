---
title: Contribution checklist
headerTitle: Contribution checklist
linkTitle: Contribution checklist
description: Review the steps to start contributing code and documentation.
headcontent: Checklist for contributing to the core database.
menu:
  preview:
    identifier: contribute-checklist
    parent: core-database
    weight: 2911
type: docs
---

## Step 1. Build the source

* First, clone [the YugabyteDB GitHub repo](https://github.com/yugabyte/yugabyte-db).

    ```bash
    git clone https://github.com/yugabyte/yugabyte-db.git
    ```

* Next, [build the source code](../build-from-src).
* Optionally, you may want to [run the unit tests](../build-and-test#test).

## Step 2. Start a local cluster

Having built the source, you can [start a local cluster](../../../quick-start/).

## Step 3. Make the change

You should now make your change, recompile the code and test out your change.

{{< note title="Note" >}}

You should read the [code style guide](../coding-style).

{{< /note >}}

## Step 4. Add unit tests

Depending on the change, you should add unit tests to make sure they do not break as the codebase is modified.

## Step 5. Re-run unit tests

Re-run the unit tests with your changes and make sure all tests pass.

## Step 6. Submit a pull request

Congratulations on the change! You should now submit a pull request for a code review and leave a message on the Slack channel. Once the code review passes, your code will get merged in.
