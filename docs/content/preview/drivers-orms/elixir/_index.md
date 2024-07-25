---
title: Elixir drivers and ORMs
headerTitle: Elixir
linkTitle: Elixir
description: Elixir Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: elixir-drivers
    parent: drivers-orms
    weight: 540
type: indexpage
showRightNav: true
---

## Supported projects

The following projects can be used to implement Elixir applications using the YugabyteDB YSQL APIs.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| ------- | ------------------------ | ------------------------ | ---------------------|
| Postgrex Driver | [Documentation](../../tutorials/build-apps/elixir/cloud-ysql-elixir/) | [v0.18.0](https://github.com/elixir-ecto/postgrex) | |
| Phoenix Framework | [Documentation](phoenix/) | [1.7.14](https://www.phoenixframework.org) | |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to a [sample Elixir app using Postgrex](../../tutorials/build-apps/elixir/cloud-ysql-elixir/) or building an app using [Phoenix framework](phoenix/).

## Prerequisites

To develop Elixir applications for YugabyteDB, you need the following:

- **Elixir**\
  Install the latest versions of [Elixir, Erlang VM, IEx and Mix](https://elixir-lang.org/docs.html) (tested with Elixir 1.17.1 and Erlang/OTP 26 erts-14.2.5).

- **YugabyteDB cluster**
  - Create a free cluster on YugabyteDB Aeon. Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Simple app using Phoenix framework](phoenix/)
