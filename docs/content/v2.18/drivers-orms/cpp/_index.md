---
title: Build apps with C++ Drivers and ORMs
headerTitle: C++
linkTitle: C++
description: C++ Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.18:
    identifier: cpp-drivers
    parent: drivers-orms
    weight: 550
type: indexpage
showRightNav: true
---

## Supported projects

The following projects are recommended for implementing Rust applications using the YugabyteDB YSQL/YCQL API.

| Project | Example apps |
| :------ | :----------- |
| libpqxx C++ Driver | [Hello World](ysql/) |
| YugabyteDB C++ driver for YCQL | [Hello World](ycql/) |

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