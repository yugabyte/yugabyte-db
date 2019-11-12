---
title: Get involved with YugabyteDB
linkTitle: Get involved
description: Get involved with YugabyteDB
image: /images/section_icons/index/quick_start.png
headcontent: Contributing code to improve YugabyteDB.
type: page
section: CONTRIBUTE
menu:
  latest:
    identifier: contribute-to-yugabyte-db
    weight: 2900
---

The Yugabyte team are strong supporters of open source. [YugabyteDB](https://github.com/yugabyte/yugabyte-db) is distributed under the Apache v2.0 license, which is very permissive open source license. We value external contributions and welcome them! To contribute, you can submit GitHub pull requests. This page contains everything you need to get you going quickly.

## Learn about the architecture

Read and review the [YugabyteDB architecture](../architecture/) documentation, which will give an overview of the high level components in the database.

## Sign the CLA

Before your first contribution is accepted, you should complete the online form [Yugabyte CLA (Contributor License Agreement)](https://docs.google.com/forms/d/11hn-vBGhOZRunclC3NKmSX1cvQVrU--r0ldDLqasRIo/edit).

## Choose an area

There are a few areas you can get involved in depending on your interests.

### Core database

C++ code and the unit tests comprise the core of YugabyteDB. Review the [Contribution checklist](core-database/checklist) to get started building the codebase and running the unit tests.

### Documentation

Yugabyte uses the Hugo static site generator to build the YugabyteDB documentation. There are two types of docs issues - infrastructure enhancements and adding content. You can [follow the steps outlined here](https://github.com/yugabyte/docs) to run a local version of the docs site. Next, look at the [contributing guide](https://github.com/yugabyte/docs/blob/master/CONTRIBUTING.md) to make your changes and contribute them.

## Find an issue

Issues are tagged with other useful labels as described below.

| Key Labels         |  Comments      |
| ------------------ | -------------- |
| `bug`              | Bugs can have a small or large scope, so make sure to understand the bug. |
| `docs`             | An issue related to the docs infrastructure, or content that needs to be added to the docs. |
| `enhancement`      | An enhancement is often done to an existing feature, and is usually limited in scope. |
| `good first issue` | A great first issue to work on as your initial contribution to YugabyteDB. |
| `help wanted`      | Issues that are very useful and relatively standalone, but not actively being worked on. |
| `new feature`      | An new feature does not exist. New features can be complex additions to the existing system, or standalone pieces. |
| `question`         | A question that needs to be answered. |

* If you are just starting out contributing to YugabyteDB, welcome and thanks! For ideas on where to get started, take a look at issues labelled [good first issue](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3A%22good+first+issue%22).

* If you have already contributed and are familiar, then review the issues labelled [help wanted](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3A%22help+wanted%22).

{{< note title="Note" >}}

If you want to work on a feature and are not sure which feature to pick, or how to proceed with the one you picked, do not hesitate to ask for help in our [Slack chat](https://www.yugabyte.com/slack) or our [community forum](https://forum.yugabyte.com/).

{{< /note >}}
