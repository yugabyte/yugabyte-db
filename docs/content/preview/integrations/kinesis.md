---
title: Kinesis Data Streams
linkTitle: Kinesis Data Streams
description: Use Kinesis Data Streams with YSQL API
aliases:
menu:
  preview_integrations:
    identifier: kinesis
    parent: data-integration
    weight: 571
type: docs
---

[Amazon Kinesis](https://aws.amazon.com/kinesis/) Data Streams enables applications to collect and process large streams of data records in real time. These applications can send the processed records to dashboards, use them to generate alerts, dynamically change pricing and advertising strategies, or send data to a variety of other AWS services.

YugabyteDB has a [Debezium connector](https://github.com/yugabyte/debezium-connector-yugabytedb) which you can use to read changes to a table and then write those into Kinesis Data Streams using the AWS SDK for Java and the Kinesis Producer Library.

## Connect

To connect your YugabyteDB database to Kinesis:

1. Create a [Kinesis Data Stream](https://docs.aws.amazon.com/streams/latest/dev/tutorial-stock-data-kplkcl-create-stream.html) from AWS Management Console. Note the name of the stream you create.
1. Start a YugabyteDB cluster. Refer [YugabyteDB Prerequisites](../tools/#yugabytedb-prerequisites).
1. Create a CDC stream using following command:

    ```sh
    ./bin/yb-admin --master_addresses 127.0.0.1:7100 create_change_data_stream ysql.yugabyte
    ```

1. Create a table using [ysqlsh](../../admin/ysqlsh/#starting-ysqlsh) as follows:

    ```sql
    CREATE TABLE users(
    id         bigserial PRIMARY KEY, created_at timestamp,
    name       text, email      text, address    text,
    city       text, state      text, zip        text,
    birth_date text, latitude   float, longitude  float,
    password   text, source     text);
    ```

1. Write a Java application which will use the Debezium connector to receive CDC data from YugabyteDB and write to Kinesis Data Streams.
The following code snippet shows an example implementation:

    ```java
    // Build Kinesis client

    AmazonKinesisClientBuilder clientBuilder = AmazonKinesisClientBuilder.standard();
    String ak = System.getenv("AWS_ACCESS_KEY_ID");
    String sk = System.getenv("AWS_SECRET_ACCESS_KEY");
    AWSCredentials ac = new BasicAWSCredentials(ak, sk);
    AWSCredentialsProvider cp = new AWSStaticCredentialsProvider(ac);
    ClientConfiguration cc = new ClientConfiguration();

    clientBuilder.setRegion("us-west-2");
    clientBuilder.setCredentials(cp);
    clientBuilder.setClientConfiguration(cc);

    AmazonKinesis kinesisClient = clientBuilder.build();

    private String lastSequenceNumber; // used while writing into Kinesis Data Stream


    // Configure properties
    Properties props = new Properties();
    props.setProperty("name", "engine");
    props.setProperty("offset.storage", "org.apache.kafka.connect.storage.FileOffsetBackingStore");
    props.setProperty("offset.storage.file.filename", "/tmp/offsets.dat");
    props.setProperty("offset.flush.interval.ms", "60000");
    props.setProperty("connector.class", "io.debezium.connector.yugabytedb.YugabyteDBConnector
    ");

    props.setProperty("database.streamid", <stream_id>); // provide stream id generated above
    props.setProperty("database.master.addresses", <master_address>); // provide master address, for local cluster it is 127.0.0.1:7100
    props.setProperty("table.include.list", "public.users");
    props.setProperty("database.hostname", <hostname>); // provide hostname, for local cluster it is 127.0.0.1
    props.setProperty("database.port", "5433");
    props.setProperty("database.user", "yugabyte");
    props.setProperty("database.password","yugabyte");
    props.setProperty("database.dbname", "yugabyte");
    props.setProperty("database.server.name", "dbserver1");
    props.setProperty("snapshot.mode", "never");

    // Create Debezium engine

    DebeziumEngine<ChangeEvent<String, String>> engine = DebeziumEngine.create(Json.class)
           .using(props)
           .notifying(record -> {
             try {
               System.out.println("Topic: " + record.destination());
               putRecordIntoKinesis(record.value());
             } catch (Throwable t) {
               System.out.println("Failed putRecord: " + t);
             }
           }).build()

    // code in putRecordIntoKinesis():
    // Use city name as partition key
    String pk = "NO CITY";
      try {
        JSONObject o = (JSONObject) new JSONParser().parse(record);
        JSONObject payload = (JSONObject) o.get("payload");
        JSONObject record = (JSONObject) payload.get("after");
        Map<String, String> city = (Map) record.get("city");
        System.out.println("Found city: " + city.get("value"));
        pk = city.get("value");
      } catch (ParseException pe) {
        System.out.println("Failed record: " + record);
      }

       PutRecordRequest putRecordRequest = new PutRecordRequest();
       putRecordRequest.setStreamName( "yugabytedb-to-kinesis" ); // This is the name of data stream created on AWS
       putRecordRequest.setData(ByteBuffer.wrap( record.getBytes() ));
       putRecordRequest.setPartitionKey( pk );
       putRecordRequest.setSequenceNumberForOrdering( lastSequenceNumber );
       PutRecordResult putRecordResult = kinesisClient.putRecord( putRecordRequest );
       lastSequenceNumber = putRecordResult.getSequenceNumber();

    // Now run the engine asynchronously
    ExecutorService executor = Executors.newSingleThreadExecutor();
    executor.execute(engine);
    ```

1. As you insert records into the users table, you can check the records arriving in the Kinesis data stream.
For some INSERT DMLs, refer to the `users.sql` script in [CDC examples](https://github.com/yugabyte/cdc-examples/blob/main/cdc-quickstart-kafka-connect/scripts/users.sql).
