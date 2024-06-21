---
title: Node.js drivers and ORMs
headerTitle: Node.js
linkTitle: Node.js
description: Node.js Drivers and ORMs support for YugabyteDB.
image: /images/section_icons/sample-data/s_s1-sampledata-3x.png
menu:
  v2.18:
    identifier: nodejs-drivers
    parent: drivers-orms
    weight: 530
type: indexpage
showRightNav: true
---

## Supported projects

The following projects are recommended for implementing Node applications using the YugabyteDB YSQL and YCQL APIs.

| Project | Documentation and Guides | Latest Driver Version | Supported YugabyteDB Version |
| :------ | :----------------------- | :----------------------- | :--------------------|
| YugabyteDB node-postgres Smart Driver| [Documentation](yugabyte-node-driver/) <br /> | [8.7.3-yb-1](https://www.npmjs.com/package/pg) | 2.8 and above |
| PostgreSQL node-postgres Driver| [Documentation](postgres-node-driver/) <br /> | [8.7.3](https://www.npmjs.com/package/pg) | 2.6 and above |
| YugabyteDB Node.js Driver for YCQL | [Documentation](ycql/) | | |

| Project | Documentation and Guides | Example Apps |
| :------ | :----------------------- | :----------- |
| Sequelize | [Documentation](sequelize/) <br /> [Hello World](../orms/nodejs/ysql-sequelize/) | [Sequelize ORM App](https://github.com/YugabyteDB-Samples/orm-examples/tree/master/node/sequelize) |
| Prisma | [Documentation](prisma/) <br /> [Hello World](../orms/nodejs/ysql-prisma/) <br /> | [Prisma ORM App](https://github.com/yugabyte/orm-examples/tree/master/node/prisma)

Learn how to establish a connection to a YugabyteDB database and begin basic CRUD operations by referring to [Connect an app](yugabyte-node-driver/) or [Use an ORM](sequelize/).

For reference documentation, including using projects with SSL, refer to the [drivers and ORMs reference](../../reference/drivers/nodejs/yugabyte-pg-reference/) pages.

## Prerequisites

To develop Node.js applications for YugabyteDB, you need the following:

- **Node.js**\
  To download and install Node.js, refer to the [Node.js](https://nodejs.org/en/download/) documentation.\
  To check the version of node, use the following command:

  ```sh
  node -v
  ```

- **Create a node.js project**\
  Create a file with the `.js` extension (for example `app.js`), which can be run using the following command:

  ```sh
  node app.js
  ```

- **YugabyteDB cluster**
  - Create a free cluster on [YugabyteDB Aeon](https://www.yugabyte.com/cloud/). Refer to [Use a cloud cluster](../../quick-start-yugabytedb-managed/). Note that YugabyteDB Aeon requires SSL.
  - Alternatively, set up a standalone YugabyteDB cluster by following the steps in [Install YugabyteDB](../../quick-start/).

## Next step

- [Connect an app](yugabyte-node-driver/)
