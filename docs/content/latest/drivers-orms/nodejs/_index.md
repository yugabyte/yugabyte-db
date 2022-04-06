---
title: NodeJS
headerTitle: NodeJS
linkTitle: NodeJS
description: NodeJS Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  latest:
    identifier: nodejs-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---
Following are the recommended projects for implementing Node Applications for YugabyteDB YSQL API.

| Project | Type | Support Level |
| :------ | :--- | :------------ |
| [Node Postgres](postgres-node-driver) | Node.JS Driver | Full |
| [Sequelize](sequelize) | ORM | Full |

## Build a Hello World App

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/latest/quick-start/build-apps/nodejs/) in the Quick Start section.

## Pre-requisites for Building a Node.JS Application

### Install Node.JS

Make sure that your system has Node.JS installed. To download and install, please refer [nodejs offical website](https://nodejs.org/en/download/).

To check the version of installed node, use the following command:

```sh
node -v
```

### Create a Node.JS Project

Create a file with `.js` extension in filename, for example `app.js` which can be run using command:

```sh
node app.js
```

### Create a YugabyteDB Cluster

Set up a Free tier Cluster on [Yugabyte Anywhere](https://www.yugabyte.com/cloud/). The free cluster provides a fully functioning YugabyteDB cluster deployed to the cloud region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing the Java Applications with YugabyteDB database. Complete the steps for [creating a free tier cluster](/latest/yugabyte-cloud/cloud-quickstart/qs-add/).

Alternatively, You can also setup a standalone YugabyteDB cluster by following the [install YugabyteDB Steps](/latest/quick-start/install/macos).

## Usage Examples

For fully runnable code snippets and exaplantion for common operations, see the specific Java driver and ORM section. Below table provides quick links for navigating to driver specific documentation and also the usage examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [Node Postgres Driver](postgres-node-driver/) | Node.JS Driver | [Hello World](/latest/quick-start/build-apps/nodejs/ysql-pg/) <br />[CRUD App](postgres-node-driver)
| [Sequelize](sequelize/) | JDBC Driver | [Hello World](/latest/quick-start/build-apps/nodejs/ysql-sequelize/) <br />[CRUD App](sequelize)
