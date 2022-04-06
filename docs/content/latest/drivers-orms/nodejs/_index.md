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

Make sure that your system has Node.JS installed. To download and install, refer to the [nodejs official website](https://nodejs.org/en/download/).

To check the version of installed node, use the following command:

```sh
node -v
```

### Create a Node.JS Project

Create a file with the `.js` extension, for example `app.js`, which can be run using the following command:

```sh
node app.js
```

### Create a YugabyteDB Cluster

Create a free cluster on Yugabyte Cloud. Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/latest/quick-start/install/macos).

## Usage Examples

For fully runnable code snippets and explanations of common operations, see the specific Java driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Usage Examples |
| :------ | :--- | :------------- |
| [Node PostgreSQL Driver](postgres-node-driver/) | Node.JS Driver | [Hello World](/latest/quick-start/build-apps/nodejs/ysql-pg/) <br />[CRUD App](postgres-node-driver)
| [Sequelize](sequelize/) | ORM | [Hello World](/latest/quick-start/build-apps/nodejs/ysql-sequelize/) <br />[CRUD App](sequelize)
