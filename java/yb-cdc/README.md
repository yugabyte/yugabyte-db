Using YugabyteDB’s Change Data Capture (CDC) API, follow these steps to use YugabyteDB as a data source to a console sink: 

### Setup YugabyteDB

1. Download latest version of YugabyteDB.

https://download.yugabyte.com/

2. Create a local cluster.

https://docs.yugabyte.com/latest/quick-start/create-local-cluster/

3. Create a table on the cluster.

### Setup the CDC Connector for stdout

1. Download the cdc connector for stdout.

```sh
wget http://download.yugabyte.com/yb-cdc-connector.jar
```

2. Start the console connector app.

```
java -jar yb_cdc_connector.jar
--table_name <namespace>.<table>
--master_addrs <yb master addresses> [default 127.0.0.1:7100]
--[stream_id] <optional existing stream id>
```

3. In another window, use cqlsh, ysqlsh, or another client to write data to the table and observe 
the values on the connector console.

