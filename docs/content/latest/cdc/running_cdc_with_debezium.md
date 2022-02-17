# Running CDC with Debezium locally

### 1. Start Zookeeper

```bash
docker run -it --rm --name zookeeper -p 2181:2181 -p 2888:2888 -p 3888:3888 debezium/zookeeper:1.6
```

### 2. Start Kafka

```bash
docker run -it --rm --name kafka -p 9092:9092 --link zookeeper:zookeeper debezium/kafka:1.6
```

### 3. Start yugabyted

Now you have to run yugabyted with the IP of your machine otherwise it would consider localhost (which would then be mapped to the docker one instead of your machine).

Note that you need a YugabyteDB version which has the changes for CDC. <br>

Clone the repository:
```bash
git clone git@github.com:yugabyte/yugabyte-db.git
```

Navigate to the repo and build the code:
```bash
cd yugabyte-db

./yb_build.sh
```

Now you have to start the yugabyted cluster locally. Get your local IP in a variable:
```bash
# For bash
echo "export IP=$(ipconfig getifaddr en0)" >> ~/.bash_profile  # If using MacOS
echo "export IP=$(hostname -i)" >> ~/.bash_profile  # If on Linux
source ~/.bash_profile

# For zsh
echo "export IP=$(ipconfig getifaddr en0)" >> ~/.zshenv  # If using MacOS
echo "export IP=$(hostname -i)" >> ~/.zshenv  # If on Linux
source ~/.zshenv
```

Start yugabyted:
```bash
./bin/yugabyted start --listen $IP
```

Connect to ysqlsh and create a table:
```bash
./bin/ysqlsh -h $IP

yugabyte=# create table test (id int primary key, name text, days_worked bigint);
```

### 4. Create a DB stream ID using yb-admin

yb-admin is equipped with the commands to manage stream IDs for Change Data Capture. Create a stream ID using the same:
```bash
./yb-admin --master_addresses ${IP}:7100 create_change_data_stream ysql.yugabyte
CDC Stream ID: d540f5e4890c4d3b812933cbfd703ed3
```

### 5. Get the Debezium connector for YugabyteDB

You can get the connector from Quay:
```bash
docker pull quay.io/yugabyte/debezium-connector:1.1-beta
```

Alternatively, if you want to build the connector yourself, follow these steps:
1. Create a directory
    ```bash
    mkdir ~/custom-connector
    ```
2. Build the code for yugabyte-db, this will give you a jar file under `yugabyte-db/java/yb-client/target/`, you need to copy it to `custom-connector`
    ```bash
    cp yugabyte-db/java/yb-client/yb-client-0.8.15-SNAPSHOT-jar-with-dependencies.jar ~/custom-connector/
    ````
3. Now clone the Debezium repo:
    ```bash
    git clone git@github.com:yugabyte/debezium.git
    
    # Navigate to the debezium repo
    cd debezium
    ```
4. Build Debezium jar:
    ```bash
    mvn clean verify -Dquick
    ```
5. Now you need to copy a few jar files to the `custom-connector` directory you created:
    ```bash
    cp debezium-core/target/debezium-core-1.7.0-SNAPSHOT.jar ~/custom-connector/
    cp debezium-api/target/debezium-api-1.7.0-SNAPSHOT.jar ~/custom-connector/
    cp debezium-connector-yugabytedb2/target/debezium-connector-yugabytedb2-1.7.0-SNAPSHOT.jar ~/custom-conector/
    ```
    You also need to get the Kafka Connect JDBC jar inside the custom-connector:
    ```bash
    # Navigate to custom-connector
    cd ~/custom-connector
    
    wget https://packages.confluent.io/maven/io/confluent/kafka-connect-jdbc/10.2.5/kafka-connect-jdbc-10.2.5.jar
    ```
6. Create a `Dockerfile` with the following contents:
    ```Dockerfile
    FROM debezium/connect:1.6
    ENV KAFKA_CONNECT_YB_DIR=$KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb
    
    # Deploy Kafka Connect yugabytedb
    RUN mkdir $KAFKA_CONNECT_YB_DIR && cd $KAFKA_CONNECT_YB_DIR

    COPY debezium-connector-yugabytedb2-1.7.0-SNAPSHOT.jar $KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb/
    COPY debezium-core-1.7.0-SNAPSHOT.jar $KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb/
    COPY debezium-api-1.7.0-SNAPSHOT.jar $KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb/
    COPY yb-client-0.8.15-SNAPSHOT-jar-with-dependencies.jar $KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb/
    COPY kafka-connect-jdbc-10.2.5.jar $KAFKA_CONNECT_PLUGINS_DIR/debezium-connector-yugabytedb/
    ```
7. Build the connector:
    ```bash
    docker build . -t yb-test-connector
    ```

### 6. Start Debezium

```bash
# If you have pulled from Quay, follow this:
docker run -it --rm --name connect -p 8083:8083 -e GROUP_ID=1 -e CONFIG_STORAGE_TOPIC=my_connect_configs \
-e OFFSET_STORAGE_TOPIC=my_connect_offsets -e STATUS_STORAGE_TOPIC=my_connect_statuses \
--link zookeeper:zookeeper --link kafka:kafka \
quay.io/yugabyte/debezium-connector:1.1-beta

# If you have built it yourself
docker run -it --rm --name connect -p 8083:8083 -e GROUP_ID=1 -e CONFIG_STORAGE_TOPIC=my_connect_configs \
-e OFFSET_STORAGE_TOPIC=my_connect_offsets -e STATUS_STORAGE_TOPIC=my_connect_statuses \
--link zookeeper:zookeeper --link kafka:kafka \
yb-test-connector
```

You now need to deploy the configuration for the connector:
```bash
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" localhost:8083/connectors/ -d '{
  "name": "ybconnector",
  "config": {
    "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",
    "database.hostname":"'$IP'",
    "database.port":"5433",
    "database.master.addresses": "'$IP':7100",
    "database.user": "yugabyte",
    "database.password": "yugabyte",
    "database.dbname" : "yugabyte",
    "database.server.name": "dbserver1",
    "table.include.list":"public.test",
    "database.streamid":"d540f5e4890c4d3b812933cbfd703ed3",
    "snapshot.mode":"never"
  }
}'
```

#### There are the following configurations which we have with the Debezium connector:
| **Property**               | **Default value**                                    | **Description**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
|----------------------------|------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| connector.class            | io.debezium.connector.yugabytedb.YugabyteDBConnector | This specifies the connector we will be using to connect Debezium to the database, in our case, since we will be connecting to YugabyteDB, we will be using the connector specified for that.                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| database.hostname          |                                                      | The IP of the host machine where the database is hosted at. If it's a distributed cluster, it's preferable to pass the IP of the leader node.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| database.port              |                                                      | The port at which the YSQL process is running                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| database.master.addresses  |                                                      | A list of the comma separated values in the form of host:port                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| database.user              |                                                      | The user which will be used to connect to the the database                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| database.password          |                                                      | Password for the given user                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| database.dbname            |                                                      | The database on which the streaming is to be done at                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| database.server.name       |                                                      | Logical name that identifies and provides a namespace for the particular Yugabyte database server or cluster in which Debezium is capturing changes. It should be unique since it is used to form the Kafka topic also.                                                                                                                                                                                                                                                                                                                                                                                                                      |
| database.streamid          |                                                      | This is the stream ID you created using yb-admin for Change Data Capture                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| table.include.list         |                                                      | Pass the name of tables in a form of comma separated values along with their schema names i.e. public.test or test_schema.test_table_name                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| snapshot.mode              |                                                      |  `never` - Don't take a snapshot <br/><br/>  `initial` - Take a snapshot when the connector is first started <br/><br/>  `always` - Always take a snapshot <br/><br/>                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| table.max.num.tablets      | 10                                                   | This is the maximum number of tablets the connector can poll for. Ideally it should be greater than or equal to the number of tablets the table is split into.                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| cdc.poll.interval.ms       | 200                                                  | The interval at which the connector will poll the database for the changes.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| admin.operation.timeout.ms | 60000                                                | This specifies the timeout for the admin operations to complete.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| operation.timeout.ms       | 60000                                                |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| socket.read.timeout.ms     | 60000                                                |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| decimal.handling.mode      | double                                               | We do not support the `precise` mode currently. <br/><br/>  `double` will map all the numeric, double and money types as Java double values (FLOAT64) <br/><br/>  `string` will represent the numeric, double and money types as their string formatted form <br/><br/>                                                                                                                                                                                                                                                                                                                                                                      |
| binary.handling.mode       | hex                                                  | We only support `hex` mode as we convert all the binary strings to their respective hex format and emit them as their string representation only.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| database.sslmode           | disable                                              | This specifies whether to use an encrypted connection to the Yugabyte cluster. The supported options are:<br/><br/>  `disable` uses an unencrypted connection <br/><br/>  `require` uses an encrypted connection and fails if it cannot be established <br/><br/>  `verify-ca` uses an encrypted connection but also verifies the server TLS certificate against the configured Certificate Authority (CA) certificates, or fails if no valid matching CA certificates are found <br/><br/>  `verify-full` behaves like verify-ca but also verifies that the server certificate matches the host to which the connector is trying to connect |
| database.sslrootcert       |                                                      | The path to the file which contains the root certificate against which the server is to be validated.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| database.sslcert           |                                                      | Path to the file containing SSL certificate to the client.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| database.sslkey            |                                                      | Path to the file containing the private key of the client.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |

### 7. Start the Postgres sink

```bash
docker run -it --rm --name postgresql -p 5432:5432 -e POSTGRES_USER=postgres \ 
-e POSTGRES_PASSWORD=postgres debezium/example-postgres:1.6

# Start the postgres terminal and when asked for a password, enter 'postgres'
docker run -it --rm --name postgresqlterm --link postgresql:postgresql --rm postgres:11.2 \ 
sh -c 'exec psql -h postgresql  -p "$POSTGRES_PORT_5432_TCP_PORT" -U postgres'
```

Deploy the JDBC sink connector properties:
```bash
curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" localhost:8083/connectors/ -d '{
  "name": "jdbc-sink",
  "config": {
    "connector.class": "io.confluent.connect.jdbc.JdbcSinkConnector",    
     "tasks.max": "1",
      "topics": "dbserver1.public.test",
      "dialect.name": "PostgreSqlDatabaseDialect",    
      "table.name.format": "destination",    
      "connection.url": "jdbc:postgresql://'$IP':5432/postgres?user=postgres&password=postgres&sslMode=require",    
      "transforms": "unwrap",    
      "transforms.unwrap.type": "io.debezium.transforms.ExtractNewRecordState",    
      "transforms.unwrap.drop.tombstones": "false",
      "auto.create": "true",   
      "insert.mode": "upsert",    
      "pk.fields": "id",    
      "pk.mode": "record_key",   
      "delete.enabled": "true",
      "auto.evolve":"true"  
   }
}'
```

### 8. Start a Kafka Topic console consumer (optional)

```bash
docker run -it --rm --name consumer --link zookeeper:zookeeper --link kafka:kafka debezium/kafka:1.6 \
watch-topic -a dbserver1.public.test
```

<br/><br/>
## Behaviour of datatypes with Debezium and Kafka
| **Dataype**                             | **What we insert in YSQL**                                            | **What we get in the Kafka topic**                        | **Remarks**                                                                                                                                                                                                     |
|-----------------------------------------|-----------------------------------------------------------------------|-----------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| bigint                                  | 123456                                                                | 123456                                                    |                                                                                                                                                                                                                 |
| bigserial                               | Cannot insert explicitly                                              |                                                           |                                                                                                                                                                                                                 |
| bit [ (n) ]                             | '11011'                                                               | "11011"                                                   |                                                                                                                                                                                                                 |
| bit varying [ (n) ]                     | '11011'                                                               | "11011"                                                   |                                                                                                                                                                                                                 |
| boolean                                 | FALSE                                                                 | false                                                     |                                                                                                                                                                                                                 |
| bytea                                   | E'\\001'                                                              | "\x01"                                                    |                                                                                                                                                                                                                 |
| character [ (n) ]                       | 'five5'                                                               | "five5"                                                   |                                                                                                                                                                                                                 |
| character varying [ (n) ]               | 'sampletext'                                                          | "sampletext"                                              |                                                                                                                                                                                                                 |
| cidr                                    | '10.1.0.0/16'                                                         | "10.1.0.0/16"                                             |                                                                                                                                                                                                                 |
| date                                    | '2021-11-25'                                                          | 18956                                                     | The value in the Kafka topic is the number of days since the Unix epoch eg. 1970-01-01                                                                                                                          |
| double precision                        | 567.89                                                                | 567.89                                                    |                                                                                                                                                                                                                 |
| inet                                    | '192.166.1.1'                                                         | "192.166.1.1"                                             |                                                                                                                                                                                                                 |
| integer                                 | 1                                                                     | 1                                                         |                                                                                                                                                                                                                 |
| interval [ fields ] [ (p) ]             | '2020-03-10 00:00:00':: timestamp - '2020-02-10 00:00:00':: timestamp | 2505600000000                                             | The output value coming up is the equivalent of the interval value in microseconds. So here 2505600000000 means 29 days.                                                                                        |
| json                                    | '{"first_name":"vaibhav"}'                                            | "{\"first_name\":\"vaibhav\"}"                            |                                                                                                                                                                                                                 |
| jsonb                                   | '{"first_name":"vaibhav"}'                                            | "{\"first_name\": \"vaibhav\"}"                           |                                                                                                                                                                                                                 |
| macaddr                                 | '2C:54:91:88:C9:E3'                                                   | "2c:54:91:88:c9:e3"                                       |                                                                                                                                                                                                                 |
| macaddr8                                | '22:00:5c:03:55:08:01:02'                                             | "22:00:5c:03:55:08:01:02"                                 |                                                                                                                                                                                                                 |
| money                                   | '$100.5'                                                              | 100.5                                                     |                                                                                                                                                                                                                 |
| numeric                                 | 34.56                                                                 | 34.56                                                     |                                                                                                                                                                                                                 |
| real                                    | 123.4567                                                              | 123.4567                                                  |                                                                                                                                                                                                                 |
| smallint                                | 12                                                                    | 12                                                        |                                                                                                                                                                                                                 |
| int4range                               | '(4, 14)'                                                             | "[5,14)"                                                  |                                                                                                                                                                                                                 |
| int8range                               | '(4, 150000)'                                                         | "[5,150000)"                                              |                                                                                                                                                                                                                 |
| numrange                                | '(10.45, 21.32)'                                                      | "(10.45,21.32)"                                           |                                                                                                                                                                                                                 |
| tsrange                                 | '(1970-01-01 00:00:00, 2000-01-01 12:00:00)'                          | "(\"1970-01-01 00:00:00\",\"2000-01-01 12:00:00\")"       |                                                                                                                                                                                                                 |
| tstzrange                               | '(2017-07-04 12:30:30 UTC, 2021-07-04 12:30:30+05:30)'                | "(\"2017-07-04 12:30:30+00\",\"2021-07-04 07:00:30+00\")" |                                                                                                                                                                                                                 |
| daterange                               | '(2019-10-07, 2021-10-07)'                                            | "[2019-10-08,2021-10-07)"                                 |                                                                                                                                                                                                                 |
| smallserial                             | Cannot insert explicitly                                              |                                                           |                                                                                                                                                                                                                 |
| serial                                  | Cannot insert explicitly                                              |                                                           |                                                                                                                                                                                                                 |
| text                                    | 'text to verify behaviour'                                            | "text to verify behaviour"                                |                                                                                                                                                                                                                 |
| time [ (p) ] [ without time zone ]      | '12:47:32'                                                            | 46052000                                                  | The output value is the number of milliseconds since midnight.                                                                                                                                                  |
| time [ (p) ] with time zone             | '12:00:00+05:30'                                                      | "06:30:00Z"                                               | The output value is the equivalent of the inserted time in UTC. The Z stands for Zero Timezone                                                                                                                  |
| timestamp [ (p) ] [ without time zone ] | '2021-11-25 12:00:00'                                                 | 1637841600000                                             | The output value is the number of milliseconds since the UNIX epoch i.e. 1970-01-01 midnight.                                                                                                                   |
| timestamp [ (p) ] with time zone        | '2021-11-25 12:00:00+05:30'                                           | "2021-11-25T06:30:00Z"                                    | This output value is the timestamp value in UTC wherein the Z stands for Zero Timezone and T acts as a seperator between the date and time. This format is defined by the sensible practical standard ISO 8601. |
| uuid                                    | 'ffffffff-ffff-ffff-ffff-ffffffffffff'                                | "ffffffff-ffff-ffff-ffff-ffffffffffff"                    |                                                                                                                                                                                                                 |
| tsquery                                 |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| tsvector                                |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| txid_snapshot                           |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| box                                     |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| circle                                  |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| line                                    |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| lseg                                    |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| path                                    |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| pg_lsn                                  |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| point                                   |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
| polygon                                 |                                                                       |                                                           | Not supported currently                                                                                                                                                                                         |
