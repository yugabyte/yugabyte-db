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
The following projects are recommended for implementing Node applications using the YugabyteDB YSQL API.

| Project | Type | Support | Examples |
| :------ | :--- | :------ | :------- |
| [node-postgres](postgres-node-driver) | Node.JS Driver | Full | [Hello World](/preview/quick-start/build-apps/nodejs/ysql-pg/) <br />[CRUD](postgres-node-driver) |
| [Sequelize](sequelize) | ORM | Full | [Hello World](/preview/quick-start/build-apps/nodejs/ysql-sequelize/) <br />[CRUD](sequelize) |

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the project page **CRUD** example. Before running CRUD examples, make sure you have installed the prerequisites.

## Prerequisites

To develop Node.js applications for YugabyteDB, you need the following:

- **Node.JS**\
  To download and install Node.js, refer to the [Node.js](https://nodejs.org/en/download/) documentation.\
  To check the version of node, use the following command:

  ```sh
  node -v
  ```

- **Create a Node.JS project**\
  Create a file with the `.js` extension (for example `app.js`), which can be run using the following command:

  ```sh
  node app.js
  ```

- **YugabyteDB cluster**
  - Create a free cluster on [Yugabyte Cloud](https://www.yugabyte.com/cloud/). Refer to [Create a free cluster](../../yugabyte-cloud/cloud-basics/create-clusters-free/). Note that Yugabyte Cloud requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](/preview/quick-start/install/macos).
