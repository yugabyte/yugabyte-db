---
title: PHP
headerTitle: PHP
linkTitle: PHP
description: PHP Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.20:
    identifier: php-drivers
    parent: drivers-orms
    weight: 590
type: indexpage
showRightNav: true
---

## Supported projects

The following project is recommended for implementing PHP applications using the YugabyteDB YSQL API.

| Project | Example apps |
| :------ | :----------- |
| php-pgsql Driver | [Hello World](ysql/) |

| Project | Documentation and Guides | Example Apps |
| ------- | ------------------------ | ------------ |
| Laravel ORM | [Documentation](laravel/)<br />[Hello World](../orms/php/ysql-laravel/) | [Laravel ORM App](https://github.com/yugabyte/orm-examples/tree/master/php/laravel/)

For fully-runnable code snippets and explanations of common operations, see the **example apps**. Before running the example apps, make sure you have installed the prerequisites.

## Prerequisites

To develop PHP applications for YugabyteDB, you need:

- **a YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/).
