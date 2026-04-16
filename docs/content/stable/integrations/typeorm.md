<!---
title: Using TypeORM with YugabyteDB
linkTitle: TypeORM
description: Using TypeORM with YugabyteDB
menu:
  stable_integrations:
    identifier: typeorm
    parent: integrations
    weight: 571
type: docs
--->

This document describes how to use [TypeORM](https://typeorm.io/), an ORM that can run in NodeJS, Browser, Cordova, PhoneGap, Ionic, React Native, NativeScript, Expo, and Electron platforms and can be used with TypeScript and JavaScript (ES2021), with YugabetyDB.

## Prerequisites

Before you can start using TypeORM, ensure that you have the following available:

- YugabyteDB version 2.4 or later (see [Quick Start](/stable/quick-start/macos/)).

- Node.js version 20.16.0 along with npm and npx (see [Downloading and installing Node.js](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm)).

-  pg, the Node.js driver for yugabytedb
```
npm install pg --save
```

## Using TypeORM

To start using TypeORM with YugabyteDB, first generate the starter project from the CLI. This project creates a table user and inserts a row in it, which can be seen in the `index.ts` file of the project.

```
npx typeorm init --name MyProject --database postgres
```

The next step is to install new project dependencies:

```
cd MyProject
npm install
```

After you have all dependencies installed, edit the data-source.ts file and put the following there:

```javascript
import "reflect-metadata"
import { DataSource } from "typeorm"
import { User } from "./entity/User"

export const AppDataSource = new DataSource({
    type: "postgres",
    host: "localhost",
    port: 5433,
    username: "yugabyte",
    password: "yugabyte",
    database: "yugabyte",
    synchronize: true,
    logging: false,
    entities: [User],
    migrations: [],
    subscribers: [],
})
```

To run the application, execute the following command:

```shell
npm start
```

## Testing the code

You can verify the code execution by looking for the changes inside the database, as follows:

- Navigate to your YugabyteDB installation directory by running the following command:

  ```shell
  cd /<path-to-yugabytedb>
  ```

- Run the ysqlsh client by executing the following command:

  ```sh
  ./bin/ysqlsh
  ```

- Obtain the list of all the tables in the database by executing the following command:

  ```sql
  \dt
  ```

- Check if rows have been inserted into the table by executing the following:

  ```sql
  SELECT * FROM public."user;"
  ```

  The output should be as follows:

  ```output
   id | firstName | lastName | age
   ----+-----------+----------+-----
   1 | Timber    | Saw      |  25
   (1 row)
  ```