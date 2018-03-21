
## Inserts

## Queries

### Aliases

### Result Paging

## Prepare-Binds

## Sample Java Application

You can find a working example of a simple key-value workload running on YugaByte in our [sample applications](../../../quick-start/run-sample-apps/). The app writes out unique string keys, each with a string value. There are multiple readers and writers that insert, update and read these keys in parallel.

Here is how you can try out this sample application.

```sh
Usage:
  java -jar yb-sample-apps.jar \
    --workload CassandraKeyValue \
    --nodes 127.0.0.1:9042

  Other options (with default values):
      [ --num_unique_keys 1000000 ]
      [ --num_reads -1 ]
      [ --num_writes -1 ]
      [ --value_size 0 ]
      [ --num_threads_read 24 ]
      [ --num_threads_write 2 ]
      [ --table_ttl_seconds -1 ]
```


Browse the [Java source code for the batch application](https://github.com/YugaByte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraKeyValue.java) to see how everything fits together.
