---
title: Node.js drivers and ORMs
headerTitle: Node.js
linkTitle: Node.js
description: Node.js Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.14:
    identifier: nodejs-drivers
    parent: drivers-orms
    weight: 570
type: indexpage
---

The following projects are recommended for implementing Node applications using the YugabyteDB YSQL API.

## Supported projects

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| :------ | :----------------------- | :----------------------- | :--------------------|
| node-postgres Driver| [Documentation](postgres-node-driver/) <br /> [Hello World App](../../quick-start/build-apps/nodejs/ysql-pg/) | [8.7.3](https://www.npmjs.com/package/pg) | 2.6 and above |

| Project | Documentation and Guides | Example Apps |
| :------ | :----------------------- | :----------- |
| Sequelize | [Documentation](sequelize) <br /> [Hello World App](../../quick-start/build-apps/nodejs/ysql-sequelize/) | [Sequelize ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/node/sequelize/) |
| Prisma | [Documentation](prisma) <br /> [Hello World App](../../quick-start/build-apps/nodejs/ysql-prisma/) <br /> | [Prisma ORM App](https://github.com/yugabyte/orm-examples/tree/master/node/prisma)

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations using the **Hello World** examples.

For fully-runnable code snippets and explanations of common operations, see the  **example apps**. Before running the example apps, make sure you have installed the prerequisites.

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
  - Create a free cluster on YugabyteDB Managed. Refer to [Use a cloud cluster](/preview/quick-start-yugabytedb-managed/). Note that YugabyteDB Managed requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next steps

- Learn how to build NodeJS applications using [Sequelize](sequelize/).
- Learn how to use [Prisma](prisma/) with YugabyteDB.
