## Installation

Install the nodejs driver using the following command.

```
npm install redis
```

## Working Example

### Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the CQL and Redis shell. If not, please follow these steps in the [quick start guide](/quick-start/test-cql/).
- installed a recent version of `node`. If not, you can find install instructions [here](https://nodejs.org/en/download/).


### Writing the js code

Create a file `yb-redis-helloworld.js` and add the following content to it.

```js
const redis = require("redis")
const client = redis.createClient();

client.on("error", function (err) {
    console.log("Error " + err);
});

// Insert the user profile.
const userid = 1
client.hmset(userid, ["name", "John", "age", "35", "language", "NodeJS"], redis.print);

// Query the user profile.
client.hmget(userid, ["name", "age", "language"], redis.print);

// Close the client.
client.quit(function (err, res) {
    console.log('Exiting from quit command.');
});
```

### Running the application

To run the application, type the following:

```sh
node yb-redis-helloworld.js
```

You should see the following output.

```sh
Reply: OK
Reply: John,35,NodeJS
Exiting from quit command.
```
