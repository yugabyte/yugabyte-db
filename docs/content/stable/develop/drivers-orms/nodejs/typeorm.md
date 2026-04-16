---
title: TypeORM
headerTitle: Use an ORM
linkTitle: Use an ORM
description: Node.js TypeORM support for YugabyteDB
headcontent: Node.js ORM support for YugabyteDB
aliases:
  - /integrations/typeorm/
menu:
  stable_develop:
    identifier: node-orm-3-typeorm
    parent: nodejs-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../sequelize/" class="nav-link ">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Sequelize
    </a>
  </li>
  <li >
    <a href="../prisma/" class="nav-link ">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      Prisma
    </a>
  </li>
  <li >
    <a href="../typeorm/" class="nav-link active">
      <i class="fa-brands fa-node-js" aria-hidden="true"></i>
      TypeORM
    </a>
  </li>
</ul>

[TypeORM](https://typeorm.io/) is an ORM that can run in NodeJS, Browser, Cordova, PhoneGap, Ionic, React Native, NativeScript, Expo, and Electron platforms, and can be used with TypeScript and JavaScript (ES2021). Unlike other current JavaScript ORMs, it supports both Active Record and Data Mapper patterns, which means you can write high-quality, loosely coupled, scalable, maintainable applications in the most productive way.

Because YugabyteDB is PostgreSQL-compatible, TypeORM supports the YugabyteDB YSQL API.

Use this guide to get started using TypeORM for connecting to YugabyteDB.

## CRUD operations

The following steps demonstrate how to perform common tasks required for Node.js application development using TypeORM.

### Step 1: Create a Node.js project and install TypeORM package

Before proceeding, you need to install Node.js on your machine. Refer to [Downloading and installing Node.js and npm](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm#using-a-node-installer-to-install-node-js-and-npm) in the npm documentation.

To create a basic Node.js project and install the `typeorm` package, do the following:

1. Create a new directory and initialize a Node.js project. This creates a `package.json` file.

    ```sh
    mkdir nodejs-typeorm-example && cd nodejs-typeorm-example
    npm init -y
    ```

1. Configure typescript by first creating a `tsconfig.json` file.

    ```sh
    npx tsc --init
    ```

1. Update the `tsconfig.json` file.

    ```json
    {
      "compilerOptions": {
        "target": "ES2021",
        "module": "CommonJS",
        "outDir": "./dist",
        "rootDir": "./src",
        "strict": true,
        "experimentalDecorators": true,
        "emitDecoratorMetadata": true
        }
    }
    ```

1. Install the `typeorm` package and its related dependency packages, along with typescript and the pg driver.

    ```sh
    npm install typeorm pg reflect-metadata
    npm install --save-dev typescript ts-node @types/node
    ```

    **Optional: Use YugabyteDB smart driver with TypeORM**

    For better features like load balancing, topology awareness, and multinode cluster support, you can use the [YugabyteDB smart driver](../../smart-drivers/) as a replacement for the PostgreSQL driver (TypeORM uses the `pg` driver under the hood):

    ```sh
    npm install typeorm reflect-metadata "pg@npm:@yugabytedb/pg"
    npm install --save-dev typescript ts-node @types/node
    ```

    Refer to [YugabyteDB Node.js smart driver documentation](../../nodejs/yugabyte-node-driver/) for more information on smart driver with Node.js.

    When using the smart driver, your `package.json` dependencies section will look like the following:

    ```json
    {
      "dependencies": {
        "typeorm": "^0.3.17",
        "pg": "npm:@yugabytedb/pg",
        "reflect-metadata": "^0.1.13"
      },
      "devDependencies": {
        "typescript": "^5.0.0",
        "ts-node": "^10.9.0",
        "@types/node": "^20.0.0"
      }
    }
    ```

### Step 2: Implement ORM mapping for YugabyteDB

1. To start with TypeORM, in your project directory, create a directory `src` with the following structure:

    ```sh
    mkdir src
    touch src/data-source.ts
    touch src/index.ts
    mkdir src/entity
    touch src/entity/User.ts
    ```

1. `data-source.ts` contains the credentials to connect to your database. Copy the following sample code to the `data-source.ts` file.

    **Basic configuration:**

    ```js
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

    **For multi-node clusters with load balancing (YugabyteDB Smart Driver):**

    If you're using a multi-node YugabyteDB cluster and want to take advantage of load balancing and topology awareness, use the following configuration with the smart driver:

    ```js
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
      extra: {
        loadBalance: "true"
      }
    })
    ```

    The `extra.loadBalance: "true"` option enables load balancing across multiple nodes in your YugabyteDB cluster. This configuration is only beneficial when using the YugabyteDB smart driver with a multi-node cluster setup. For single-node deployments or when not using the smart driver, you can omit this configuration and use the above one.

1. Create a new entity, User, by adding the following to the `entity/User.ts` file.

    ```js
    import { Entity, PrimaryGeneratedColumn, Column } from "typeorm";

    @Entity()
    export class User {
      @PrimaryGeneratedColumn()
      id!: number;

      @Column()
      firstname!: string;

      @Column()
      lastname!: string;

      @Column()
      age!: number;
    }
    ```

1. After the setup is done, you can connect to the database and perform the CRUD operation. In the `index.ts` file, add the following:

    ```js
    import { AppDataSource } from "./data-source"
    import { User } from "./entity/User"

    AppDataSource.initialize().then(async () => {

      console.log("Inserting a new user into the database...")
      const user = new User()
      user.firstname = "Timber"
      user.lastname = "Saw"
      user.age = 25
      await AppDataSource.manager.save(user)
      console.log("Saved a new user with id: " + user.id)

      console.log("Loading users from the database...")
      const users = await AppDataSource.manager.find(User)
      console.log("Loaded users: ", users)
      console.log("Here you can setup and run express / fastify / any other framework.")
      }).catch(error => console.log(error))
    ```

Run the `index.ts` file:

``` sh
npx ts-node src/index.ts
```

The following output is expected:

```text
Inserting a new user into the database...
Saved a new user with id: 1
Loading users from the database...
Loaded users:  [ User { id: 1, firstname: 'Timber', lastname: 'Saw', age: 25 } ]
```

## Learn more

- [YugabyteDB smart drivers for YSQL](../../smart-drivers/)
