## Pre-requisites

This tutorial assumes that you have:

- installed YugaByte DB, created a universe and are able to interact with it using the Redis shell. If not, please follow these steps in the [quick start guide](../../../quick-start/test-redis/).
- installed Go version 1.8+

## Install Go Redis Driver

To install the driver locally run:

```sh
$ go get github.com/go-redis/redis
```

## Writing a HelloWorld Redis app

Create a file `ybredis_hello_world.go` and copy the contents below.

```go
package main;

import (
  "fmt"
  "log"

  "github.com/go-redis/redis"
)

func main() {
  // Connect to the cluster.
  client := redis.NewClient(&redis.Options{
    Addr:     "localhost:6379",
    Password: "", // no password set
    DB:       0,  // use the default DB
  })
  defer client.Close()

  // Insert some data (for user id 1).
  var userid string = "1"
  ok, err := client.HMSet(userid, map[string]interface{}{
        "name": "John",
        "age": "35",
        "language": "Redis"}).Result()

  if (err != nil) {
    log.Fatal(err)
  }
  fmt.Printf("HMSET returned '%s' for id=%s with values: name=John, age=35, language=Redis\n", ok, userid)

  // Query the data.
  result, err := client.HGetAll("1").Result()
  fmt.Printf("Query result for id=%s: '%v'\n", userid, result)
}
```

## Running the app

To execute the file, run the following command:

```sh
$ go run ybredis_hello_world.go
```

You should see the following as the output.

```
HMSET returned 'OK' for id=1 with values: name=John, age=35, language=Redis
Query result for id=1: 'map[age:35 language:Redis name:John]'
```
