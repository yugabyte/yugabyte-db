---
title: Build applications with C Drivers and ORMs
headerTitle: C
linkTitle: C
description: C Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: c-drivers
    parent: drivers-orms
    weight: 540
type: indexpage
showRightNav: true
---

## Supported projects

The following project is recommended for implementing C applications using the YugabyteDB YSQL API.

| Project | Example apps |
| :------ | :----------- |
| libpq C Driver | [Hello World](ysql/) |

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop C applications for YugabyteDB, you need the following:

- **Machine and Software requirements**
  - a 32-bit (x86) or 64-bit (x64) architecture machine.
  - gcc 4.1.2 or later, clang 3.4 or later installed.

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Connect an app](ysql/)
