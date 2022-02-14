---
title: Use Arctype with YugabyteDB YSQL
headerTitle: Arctype
linkTitle: Arctype
description: Use Arctype to work with distributed SQL databases in YugabyteDB.
section: INTEGRATIONS
block_indexing: true
menu:
  latest:
    identifier: arctype
    parent: tools
    weight: 3000
isTocNested: true
showAsideToc: true
---

This document describes how to query and visualize data in YugabyteDB using [Arctype](https://arctype.com/), a user-friendly collaborative SQL client.

Arctype is a database client with a focus on speed and design. Arctype is free to use and cross platform. It offers one click query sharing for teams, and users can quickly visualize query output and even combine multiple charts and tables into a simple and intuitive dashboard.

![Arctype application](/images/develop/tools/arctype/Arctype-YB-Image-2.png)


## Before you begin

Your YugabyteDB cluster should be up and running. If you're new to YugabyteDB, create a local cluster in less than five minutes following the steps in [Quick Start](../../../quick-start/install). You also need to install Arctype client on your computer. Arctype clients are available for Windows, Linux and Mac that can be downloaded from [Arctype](https://arctype.com/) website.

## Create a database connection

