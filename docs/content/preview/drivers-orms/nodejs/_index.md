---
title: NodeJS drivers and ORMs
headerTitle: NodeJS
headcontent: Prerequisites and CRUD examples for building applications in NodeJS.
linkTitle: NodeJS
description: NodeJS Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  preview:
    identifier: nodejs-drivers
    parent: drivers-orms
    weight: 570
isTocNested: true
showAsideToc: true
---
The following projects are recommended for implementing Node applications using the YugabyteDB YSQL API. For fully runnable code snippets and explanations of common operations, see the specific Java driver and ORM section. The following table provides links to driver-specific documentation and examples.

| Project | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [node-postgres](postgres-node-driver) | Node.JS Driver | Full | [Hello World](/preview/quick-start/build-apps/nodejs/ysql-pg/) <br />[CRUD App](postgres-node-driver) |
| [Sequelize](sequelize) | ORM | Full | [Hello World](/preview/quick-start/build-apps/nodejs/ysql-sequelize/) <br />[CRUD App](sequelize) |

## Build a Hello World application

Learn how to establish a connection to YugabyteDB database and begin simple CRUD operations using the steps in [Build an Application](/preview/quick-start/build-apps/nodejs/) in the Quick Start section.

## Prerequisites for building a Node.JS application

### Install Node.JS

Make sure that your system has Node.JS installed. To download and install, refer to the [node.js official website](https://nodejs.org/en/download/).

To check the version of installed node, use the following command:

```sh
node -v
```

### Create a Node.JS project

Create a file with the `.js` extension, for example `app.js`, which can be run using the following command:

```sh
node app.js
```

### Create a YugabyteDB cluster

Create a free cluster on Yugabyte Cloud. Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/).

You can also set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).
