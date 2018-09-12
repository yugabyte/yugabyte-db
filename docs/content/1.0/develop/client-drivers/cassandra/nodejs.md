
## Installation

Install the nodejs driver using the following command. You can find the source for the driver [here](https://github.com/datastax/nodejs-driver).

```sh
npm install cassandra-driver
```

## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the CQL shell. If not, please follow these steps in the [quick start guide](/quick-start/test-cassandra/).
- installed a recent version of `node`. If not, you can find install instructions [here](https://nodejs.org/en/download/).

We will be using the [async](https://github.com/caolan/async) JS utility to work with asynchronous Javascript. Install this by running the following command:

```sh
npm install --save async
```


### Writing the js code

Create a file `yb-cql-helloworld.js` and add the following content to it.

```js
const cassandra = require('cassandra-driver');
const async = require('async');
const assert = require('assert');

// Create a YB CQL client.
const client = new cassandra.Client({ contactPoints: ['127.0.0.1'] });

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
                                                                     'language varchar);';
    // Create the table.
    console.log('Creating table employee');
    client.execute(create_table, next);
  },
  function insert(next) {
    // Create a variable with the insert statement.
    const insert = "INSERT INTO ybdemo.employee (id, name, age, language) " +
                                        "VALUES (1, 'John', 35, 'NodeJS');";
    // Insert a row with the employee data.
    console.log('Inserting row with: %s', insert)
    client.execute(insert, next);
  },
  function select(next) {

    // Query the row for employee id 1 and print the results to the console.
    const select = 'SELECT name, age, language FROM ybdemo.employee WHERE id = 1;';
    client.execute(select, function (err, result) {
      if (err) return next(err);
      var row = result.first();
      console.log('Query for id=1 returned: name=%s, age=%d, language=%s',
                                            row.name, row.age, row.language);
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

### Running the application

To run the application, type the following:

```sh
node yb-cql-helloworld.js
```

You should see the following output.

```sh
Creating keyspace ybdemo
Creating table employee
Inserting row with: INSERT INTO ybdemo.employee (id, name, age, language) VALUES (1, 'John', 35, 'NodeJS');
Query for id=1 returned: name=John, age=35, language=NodeJS
Shutting down
```
