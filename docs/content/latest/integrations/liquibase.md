---
title: Using Liquibase with YugabyteDB
linkTitle: Liquibase
description: Using Liquibase with YugabyteDB
aliases:
menu:
  latest:
    identifier: liquibase
    parent: integrations
    weight: 578
isTocNested: true
showAsideToc: true
---

This document describes how to migrate data using [Liquibase](https://www.liquibase.com/) with YugabyteDB.

## Prerequisites

Before you can start using Liquibase, ensure that you have the following installed and configured:

- JDK version 8 or later.
- YugabyteDB (see [YugabyteDB Quick Start Guide](/latest/quick-start/)).
- Yugabyte cluster (see [Create a local cluster](https://docs.yugabyte.com/latest/quick-start/create-local-cluster/macos/)).



## Configuring Kafka

You configure Kafka Connect Sink as follows: