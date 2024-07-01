---
title: Node.js Driver for YCQL
headerTitle: Connect an application
linkTitle: Connect an app
description: Connect an application using YugabyteDB Node.js driver for YCQL
menu:
  preview:
    identifier: ycql-node-driver
    parent: nodejs-drivers
    weight: 600
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../yugabyte-node-driver/" class="nav-link">
      YSQL
    </a>
  </li>
  <li class="active">
    <a href="../ycql/" class="nav-link">
      YCQL
    </a>
  </li>
</ul>

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YugabyteDB Node.js Driver
    </a>
  </li>
</ul>

[Yugabyte Node.js Driver for YCQL](https://github.com/yugabyte/cassandra-java-driver/tree/4.15.x) is based on [DataStax Node.js Driver](https://github.com/datastax/nodejs-driver) for [YCQL](../../../api/ycql/) with additional [smart driver](../../smart-drivers-ycql/) features.

{{< note title="YugabyteDB Aeon" >}}

To use the driver's partition-aware load balancing feature in a YugabyteDB Aeon cluster, applications must be deployed in a VPC that has been peered with the cluster VPC so that they have access to all nodes in the cluster. For more information, refer to [Using YCQL drivers with YugabyteDB Aeon](../../smart-drivers-ycql/#using-ycql-drivers-with-yugabytedb-aeon).

{{< /note >}}

## Install the Yugabyte Node.js Driver for YCQL

To install the [YugabyteDB Node.js driver for YCQL](https://github.com/yugabyte/cassandra-nodejs-driver), run the following command:

```sh
$ npm install yb-ycql-driver
```

## Create the sample Node.js application

### Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB and created a cluster. Refer to [Quick start](../../../../quick-start/).
- installed a recent version of [Node.js](https://nodejs.org/en/download/).
- installed the [async](https://github.com/caolan/async) utility to work with asynchronous Javascript.

To install the async utility, run the following command:

```sh
$ npm install --save async
```

### Write the sample Node.js application

Create a file `yb-cql-helloworld.js` and add the following content to it.

```js
const ycql = require('yb-ycql-driver');
const async = require('async');
const assert = require('assert');

// Create a YB CQL client.
// DataStax Nodejs 4.0 loadbalancing default is TokenAwarePolicy with child DCAwareRoundRobinPolicy
// Need to provide localDataCenter option below or switch to RoundRobinPolicy
const loadBalancingPolicy = new ycql.policies.loadBalancing.RoundRobinPolicy ();
const client = new ycql.Client({ contactPoints: ['127.0.0.1'], policies : { loadBalancing : loadBalancingPolicy }});

async.series([
  function connect(next) {
    client.connect(next);
  },
  function createKeyspace(next) {
    console.log('Creating keyspace ybdemo');
    client.execute('CREATE KEYSPACE IF NOT EXISTS ybdemo;', next);
  },
  function createTable(next) {
    // The create table statement.
    const create_table = 'CREATE TABLE IF NOT EXISTS ybdemo.employee (id int PRIMARY KEY, ' +
                                                                     'name varchar, ' +
                                                                     'age int, ' +
                                                                     'language varchar, ' +
                                                                     'location jsonb);';
    // Create the table.
    console.log('Creating table employee');
    client.execute(create_table, next);
  },
  function insert(next) {
    // Create a variable with the insert statement.
    const insert = "INSERT INTO ybdemo.employee (id, name, age, language, location) " +
                                        "VALUES (1, 'John', 35, 'NodeJS', '{ \"city\": \"San Francisco\", \"state\": \"California\", \"lat\": 37.77, \"long\": 122.42 }');";
    // Insert a row with the employee data.
    console.log('Inserting row with: %s', insert)
    client.execute(insert, next);
  },
  function select(next) {

    // Query the row for employee id 1 and print the results to the console.
    const select = 'SELECT name, age, language, location FROM ybdemo.employee WHERE id = 1;';
    client.execute(select, function (err, result) {
      if (err) return next(err);
      var row = result.first();
      const city = row.location.city;
      console.log('Query for id=1 returned: name=%s, age=%d, language=%s, city=%s',
                                            row.name, row.age, row.language, city);
      next();
    });
  }
], function (err) {
  if (err) {
    console.error('There was an error', err.message, err.stack);
  }
  console.log('Shutting down');
  client.shutdown();
});
```

### Run the application

To use the application, run the following command:

```sh
$ node yb-cql-helloworld.js
```

You should see the following output.

```output
Creating keyspace ybdemo
Creating table employee
Inserting row with: INSERT INTO ybdemo.employee (id, name, age, language) VALUES (1, 'John', 35, 'NodeJS');
Query for id=1 returned: name=John, age=35, language=NodeJS
Shutting down
```
