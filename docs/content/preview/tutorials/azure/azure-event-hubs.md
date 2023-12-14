---
title: Real-Time Data Streaming with YugabyteDB CDC and Azure Event Hubs
headerTitle: Real-time data streaming with YugabyteDB CDC and Azure Event Hubs
linkTitle: Azure Event Hubs
description: Real-Time Data Streaming with YugabyteDB CDC and Azure Event Hubs
image: /images/tutorials/azure/icons/Event-Hubs-Icon.svg
headcontent: Stream data from YugabyteDB to Azure Event Hubs using Kafka Connect
menu:
  preview_tutorials:
    identifier: tutorials-azure-event-hubs
    parent: tutorials-azure
    weight: 70
type: docs
---

The [Azure Event Hubs](https://learn.microsoft.com/en-us/azure/event-hubs/event-hubs-about) data streaming service is [Apache Kafka](https://kafka.apache.org/intro) compatible, enabling existing workloads to easily be moved to Azure. With the [Debezium Connector for YugabyteDB](../../../explore/change-data-capture/debezium-connector-yugabytedb), we can stream changes from a YugabyteDB cluster to a Kafka topic using [Kafka Connect](https://docs.confluent.io/platform/current/connect/index.html#:~:text=Kafka%20Connect%20is%20a%20tool,in%20and%20out%20of%20Kafka.).

In this tutorial, we'll examine how the [YugabyteDB CDC](../../../explore/change-data-capture/cdc-overview/) can be used with Azure Event Hubs to stream real-time data for downstream processing.

In the following sections, you will:

1. Deploy and configure a single-node YugabyteDB cluster.
1. Configure Azure Event Hubs with an access policy.
1. Set up Kafka Connect to connect the YugabyteDB CDC to the Event Hubs.
1. Create an application to insert orders in our database and view the changes downstream.

## What you'll build

[Find the full project on GitHub](https://github.com/YugabyteDB-Samples/yugabytedb-azure-event-hubs-demo-nodejs). The project uses an eCommerce application and DB schema along with YugabyteDB CDC functionality to send data to Azure Event Hubs via Kafka Connect.

This application runs a Node.js process to insert order records to a YugabyteDB cluster at a regular, configurable interval. The records are then automatically captured and sent to Azure Event Hubs.

### Prerequisites

- An Azure Cloud account with permissions to create services
- [Download Apache Kafka](https://kafka.apache.org/downloads) version 2.12-3.2.0
- [Download YugabyteDB](https://download.yugabyte.com/#/) version 2.16.8.0
- [Download Debezium Connector for YugabyteDB](https://github.com/yugabyte/debezium-connector-yugabytedb/tree/v1.9.5.y.15) version 1.9.5.y.15
- [Node.js](https://github.com/nodejs/release#release-schedule) version 18

## Get started with YugabyteDB

With YugabyteDB downloaded on your machine, create a cluster and seed it with data:

1. Start a single-node cluster using [yugabyted](../../../reference/configuration/yugabyted/).

    ```sh
    ./path/to/bin/yugabyted start 
    ```

1. Connect to the cluster using [ysqlsh](../../../admin/ysqlsh/).

    ```sh
    ./path/to/bin/ysqlsh -U yugabyte
    ```

1. Prepare the database schema.

    ```sql
    CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

    CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    first_name VARCHAR(255),
    last_name VARCHAR(255)
    );

    CREATE TABLE products (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    brand VARCHAR(255),
    model VARCHAR(255)
    );

    CREATE TABLE orders (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES users(id),
    product_id UUID REFERENCES products(id),
    order_date TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    quantity INT NOT NULL,
    status VARCHAR(50)
    );
   ```

1. Add _users_ and _products_ to the database.

    ```sql
    INSERT INTO
    users (first_name, last_name)
    VALUES
    ('Gracia', 'Degli Antoni'),
    ('Javier', 'Hiom'),
    ('Minnaminnie', 'Este'),
    ('Hartley', 'Arrow'),
    ('Abbi', 'Gallear'),
    ('Lucila', 'Corden'),
    ('Henrietta', 'Fritschel'),
    ('Greta', 'Gething'),
    ('Raymond', 'Lowin'),
    ('Rufus', 'Gronowe');

    INSERT INTO
    products(brand, model)
    VALUES
    ('hoka one one', 'speedgoat 5'),
    ('hoka one one', 'torrent 2'),
    ('nike', 'vaporfly 3'),
    ('adidas', 'adizero adios pro 3');
   ```

## Get started on Azure Event Hubs

1. [Create an Azure Event Hubs Namespace](https://learn.microsoft.com/en-us/azure/event-hubs/event-hubs-create#create-an-event-hubs-namespace) in the Azure Web Portal.

    ![Create Azure Event Hubs Namespace](/images/tutorials/azure/azure-event-hubs/azure-event-hubs-namespace-config.png)

    {{< note title="Note" >}}
The **Standard** pricing tier is required for Kafka compatibility.
    {{< /note >}}
    {{< note title="Note" >}}
An Event Hubs instance will be created automatically by Debezium when Kafka Connect is configured. Event Hubs instances can be configured to automatically capture streaming data and store it in Azure Blob storage or Azure Data Lake Store, if desired.
    {{< /note >}}

1. Create a new Shared Access Policy in the Event Hubs Namespace with **Manage** access. This is a best practice, as opposed to using the root access key for the namespace to securely send and receive events.

    ![Create a new Shared Access Policy](/images/tutorials/azure/azure-event-hubs/azure-event-hubs-shared-access-key.png)

## Configure Kafka Connect

While Kafka's core broker functionality is being replaced by Event Hubs, Kafka Connect can still be used to connect the YugabyteDB CDC to the Event Hubs we just created. The _connect-distributed.sh_ script is used to start Kafka Connect in a distributed mode. This script can be found in the _bin_ directory of the downloaded Kafka distribution.

A Kafka Connect configuration file is required to provide information about the bootstrap servers (in this case, the Event Hubs host), cluster coordination, and data conversion settings, just to name a few. [Refer to the official documentation](https://learn.microsoft.com/en-us/azure/event-hubs/event-hubs-kafka-connect-tutorial#configure-kafka-connect-for-event-hubs) for a sample Kafka Connect configuration for Event Hubs.

1. Create a Kafka Connect configuration file named _event-hubs.config_.

    ```config
    bootstrap.servers={YOUR.EVENTHUBS.FQDN}:9093
    group.id=$Default
    # Event Hubs requires secure communication
    key.converter=org.apache.kafka.connect.json.JsonConverter
    value.converter=org.apache.kafka.connect.json.JsonConverter
    internal.key.converter=org.apache.kafka.connect.json.JsonConverter
    internal.value.converter=org.apache.kafka.connect.json.JsonConverter

    internal.key.converter.schemas.enable=false
    internal.value.converter.schemas.enable=false
    config.storage.topic=connect-cluster-configs
    offset.storage.topic=connect-cluster-offsets
    status.storage.topic=connect-cluster-status

    # required EH Kafka security settings
    security.protocol=SASL_SSL
    sasl.mechanism=PLAIN
    sasl.jaas.config=org.apache.kafka.common.security.plain.PlainLoginModule required username="$ConnectionString" password="{YOUR.EVENTHUBS.CONNECTION.STRING}";

    producer.security.protocol=SASL_SSL
    producer.sasl.mechanism=PLAIN
    producer.sasl.jaas.config=org.apache.kafka.common.security.plain.PlainLoginModule required username="$ConnectionString" password="{YOUR.EVENTHUBS.CONNECTION.STRING}";

    consumer.security.protocol=SASL_SSL
    consumer.sasl.mechanism=PLAIN
    consumer.sasl.jaas.config=org.apache.kafka.common.security.plain.PlainLoginModule required username="$ConnectionString" password="{YOUR.EVENTHUBS.CONNECTION.STRING}";
    ```

1. Replace {YOUR.EVENTHUBS.FQDN} with the Event Hubs Namespace host name.

    ![Locate domain name for Event Hubs Namespace](/images/tutorials/azure/azure-event-hubs/azure-event-hubs-host-name.png "Locate domain name for Event Hubs Namespace")

1. Replace {YOUR.EVENTHUBS.CONNECTION.STRING} with an Event Hubs connection string to your namespace.

    ![Locate connection string](/images/tutorials/azure/azure-event-hubs/azure-event-hubs-connection-string.png "Locate connection string")

1. Copy your configuration file to the Kafka _bin_ directory.

   ```sh
   cp /path/to/event-hubs.config  /path/to/kafka_2.12-3.2.0/bin
   ```

1. Copy the Debezium Connector for YugabyteDB to the Kafka _libs_ directory.

    ```sh
    cp /path/to/debezium-connector-yugabytedb-1.9.5.y.15.jar /path/to/kafka_2.12-3.2.0/libs
    ```

1. Run Kafka Connect via the _connect-distributed.sh_ script from the Kafka root directory.

    ```sh
    ./bin/connect-distributed.sh ./bin/event-hubs.config
    ```

1. [Create a CDC stream ID](../../../admin/yb-admin/#create-change-data-stream) to connect to Kafka Connect.

    ```sh
    ./bin/yb-admin --master_addresses 127.0.0.1:7100 create_change_data_stream ysql.yugabyte
    ```

    ```output
    CDC Stream ID: efb6cd0ed21346e5b0ed4bb69497dfc3
    ```

1. POST a connector for YugabyteDB with the generated CDC stream ID value.

    ```sh
    curl -i -X POST -H "Accept:application/json" -H "Content-Type:application/json" \
    localhost:8083/connectors/ \
    -d '{
    "name": "ybconnector",
    "config": {
        "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",
        "database.hostname":"127.0.0.1",
        "database.port":"5433",
        "database.master.addresses": "127.0.0.1:7100",
        "database.user": "yugabyte",
        "database.password": "yugabyte",
        "database.dbname" : "yugabyte",
        "database.server.name": "dbserver1",
        "table.include.list":"public.orders",
        "database.streamid":"{YOUR_YUGABYTEDB_CDC_STREAM_ID}",
        "snapshot.mode":"never"
    }
    }'
   ```

Now writes to the _orders_ table in the YugabyteDB cluster will be streamed to Azure Event Hubs via Kafka Connect.

{{< note title="Note" >}}
Debezium will auto-create a topic for each table included and several metadata topics. A Kafka topic corresponds to an Event Hubs instance. For more information, check out the [Kafka and Event Hubs conceptual mapping](https://learn.microsoft.com/en-us/azure/event-hubs/azure-event-hubs-kafka-overview#apache-kafka-and-azure-event-hubs-conceptual-mapping).
{{< /note >}}

## Test the application

We can test this real-time functionality by running a sample application to insert orders into our YugabyteDB instance. With a Kafka Connect configured properly to an Event Hubs namespace, we can see messages being sent to an Event Hubs instance.

1. Clone the repository.

    ```sh
    git clone git@github.com:YugabyteDB-Samples/yugabytedb-azure-event-hubs-demo-nodejs.git

    cd yugabytedb-azure-event-hubs-demo-nodejs
    ```

1. Install Node.js application dependencies.

    ```sh
    npm install
    ```

1. Review the Node.js sample application.

    ```sh
    const { Pool } = require("@yugabytedb/pg");

    const pool = new Pool({
        user: "yugabyte",
        host: "127.0.0.1",
        database: "yugabyte",
        password: "yugabyte",
        port: 5433,
        max: 10,
        idleTimeoutMillis: 0,
    });
    async function start() {
        const usersResponse = await pool.query("SELECT * from users;");
        const users = usersResponse?.rows;
        const productsResponse = await pool.query("SELECT * from products;");
        const products = productsResponse?.rows;

        setInterval(async () => {
            try {
                const randomUser = users[Math.floor(Math.random() * users.length)];
                const randomProduct =
                products[Math.floor(Math.random() * products.length)];
                const insertResponse = await pool.query(
                "INSERT INTO orders(user_id, product_id, quantity, status) VALUES ($1, $2, $3, $4) RETURNING *",
                [randomUser?.id, randomProduct?.id, 1, "processing"]
                );

                console.log("Insert Response: ", insertResponse?.rows?.[0]);
            } catch (e) {
                console.log(`Error while inserting order: ${e}`);
                
            }
        }, process.env.INSERT_FREQUENCY_MS || 50);
    }

    start();
    ```

    This application initializes a connection pool to connect to the YugabyteDB cluster using the [YugabyteDB node-postgres smart driver](../../../drivers-orms/nodejs/yugabyte-node-driver/). It then randomly inserts records into the _orders_ table at a regular interval.

1. Run the application.

    ```sh
    node index.js 
    ```

    The terminal window will begin outputting the response from YugabyteDB, indicating that the records are being inserted into the database.

    ```output
    # Example output
    Insert Response:  {
        id: '6b0dffe9-eea4-4997-a8bd-3e84e58dc4e5',
        user_id: '17246d85-a403-4aec-be83-1dd2c5d57dbb',
        product_id: 'a326aaa4-a343-45f6-b99a-d16f6ac7ad14',
        order_date: 2023-12-06T19:54:25.313Z,
        quantity: 1,
        status: 'processing'
    }
    Insert Response:  {
        id: '29ae786e-cc4d-4bf3-b64c-37825ee5b5a7',
        user_id: '7170de37-1a9f-40de-9275-38924ddec05d',
        product_id: '7354f2c3-341b-4851-a01a-e0b3b4f3c172',
        order_date: 2023-12-06T19:54:25.364Z,
        quantity: 1,
        status: 'processing'
    }
    ...
    ```

    Heading over to the Azure Event Hubs instance _database1.public.orders_, we can see that the messages are reaching Azure and can be consumed by downstream applications and services.

    ![Incoming messages](/images/tutorials/azure/azure-event-hubs/azure-event-hubs-incoming-messages.png "Incoming messages")

## Wrap-up

YugabyteDB CDC combined with Azure Event Hubs enables real-time application development using a familiar Kafka interface.

If you're interested in real-time data processing on Azure, check out [Azure Synapse Analytics integration using Azure Event Hubs](../../../explore/change-data-capture/cdc-tutorials/cdc-azure-event-hub/).
