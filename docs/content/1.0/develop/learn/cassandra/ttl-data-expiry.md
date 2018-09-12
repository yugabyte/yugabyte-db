
## Overview

## Table level TTL

## Row level TTL

## Column level TTL

## User defined timestamps

## Finding the write time

## Sample Java Application

You can find a working example of using transactions with YugaByte in our [sample applications](/quick-start/run-sample-apps/). Here is how you can try out this sample application.

```sh
 - CassandraStockTicker :
   --------------------
    Sample stock ticker app built on CQL. The app models 10,000 stock tickers
    each of which emits quote data every second. The raw data is written into the
    'stock_ticker_raw' table, which retains data for one day. The 'stock_ticker_1min'
    table models downsampled ticker data, is written to once a minute and retains data
    for 60 days. Every read query gets the latest value of the stock symbol from the
    'stock_ticker_raw' table.

    Usage:
      java -jar yb-sample-apps.jar \
      --workload CassandraStockTicker \
      --nodes 127.0.0.1:9042

    Other options (with default values):
      [ --num_threads_read 32 ]
      [ --num_threads_write 4 ]
      [ --num_ticker_symbols 10000 ]
      [ --data_emit_rate_millis 1000 ]
      [ --table_ttl_seconds 86400 ]
```


Browse the [Java source code for the batch application](https://github.com/YugaByte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraStockTicker.java) to see how everything fits together.
