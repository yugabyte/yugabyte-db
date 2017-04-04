---
date: 2016-03-09T20:08:11+01:00
title: Develop
weight: 30
---

## Run a sample app

![Time Series Sample App](/images/time-series-sample-app.png)

```sh
# go to the utils directory on the yugaware host
cd /opt/yugabyte/utils

# run the time-series sample app
java -jar yb-sample-app.jar --workload CassandraTimeseries --nodes 10.151.22.132:9042,10.151.38.209:9042,10.151.50.1:9042
```

Other values of the `workload` param are `CassandraStockTicker`, `CassandraKeyValue`, `RedisKeyValue`

## Run your own app

### Create table

\<docs coming soon\>

### Write data to table

\<docs coming soon\>

### Read data from table

\<docs coming soon\>

