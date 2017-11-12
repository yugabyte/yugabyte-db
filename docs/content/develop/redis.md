---
date: 2016-03-09T20:08:11+01:00
title: Redis Apps
weight: 56
---

## RedisKeyValue

Sample key-value Redis app that writes out 1M unique string keys each with a string value. There are multiple readers and writers that update these keys and read them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload RedisKeyValue --nodes localhost:6379
```

Other options (with default values):

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --num_threads_read 32 ]
[ --num_threads_write 2 ]
[ --nouuid]
```

The `--nouuid` option ensures that the keys are not based on an uuid for every run. The keys created with this option will be `key:1`, `key:2`, `key:3` and so on. The values corresponding to the keys would be `val:1`, `val:2`, `val:3` and so on.

## RedisPipelinedKeyValue

Sample key-value Redis app writes out 1M unique string keys each with a string value. There are multiple readers and writers that update these keys and read them indefinitely. Note that the number of reads and writes to perform can be specified as a parameter.

```sh
$ java -jar ./java/yb-sample-apps.jar --workload RedisPipelinedKeyValue --nodes localhost:6379
```

```sh
[ --num_unique_keys 1000000 ]
[ --num_reads -1 ]
[ --num_writes -1 ]
[ --num_threads_read 32 ]
[ --num_threads_write 2 ]
[ --pipeline_length 1 ]
```