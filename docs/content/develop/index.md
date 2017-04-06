---
date: 2016-03-09T20:08:11+01:00
title: Develop
weight: 30
---

## Run a sample app

YugaWare ships with sample apps for the most common use cases powered by YugaByte. You can see the instructions on how to run these apps by simply clicking on the **Apps** button in the Overview tab of the Universe detail page.

![Time Series Sample App](/images/time-series-sample-app.png)

Log into the YugaWare host machine.

```sh
# open a bash shell in the yugaware docker container
sudo docker exec -it yugaware bash

# run the time-series sample app
java -jar /opt/yugabyte/utils/yb-sample-app.jar --workload CassandraTimeseries --nodes 10.151.22.132:9042,10.151.38.209:9042,10.151.50.1:9042
```

Other values of the `workload` param are `CassandraStockTicker`, `CassandraKeyValue`, `RedisKeyValue`

## Run your own app

### Create table

\<docs coming soon\>

### Write data to table

\<docs coming soon\>

### Read data from table

\<docs coming soon\>

