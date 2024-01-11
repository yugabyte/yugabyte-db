---
title: Confluent Cloud tutorial for YugabyteDB CDC
headerTitle: Confluent Cloud
linkTitle: Confluent Cloud
description: Confluent Cloud for Change Data Capture in YugabyteDB.
headcontent:
menu:
  preview:
    parent: tutorials-kafka-stream
    identifier: cdc-confluent-cloud
    weight: 30
type: docs
---


This tutorial describes how to configure a YugabyteDB connector to publish changes to Confluent Cloud, and assumes some familiarity with Docker and YAML files.

![Architecture of YugabyteDB to Confluent Cloud pipeline](/images/explore/cdc/confluent_images/cdc_confluent_cloud.png)

### Configure a Confluent Cloud cluster

Set up a Confluent Cloud cluster on the provider of your choice. For more information, refer to [Manage Kafka Clusters on Confluent Cloud](https://docs.confluent.io/cloud/current/clusters/create-cluster.html) in the Confluent documentation.

### Download the credentials

Create an API key for your Confluent cluster and save it. Refer to [Use API Keys to Control Access in Confluent Cloud](https://docs.confluent.io/cloud/current/access-management/authenticate/api-keys/api-keys.html) for instructions.

### Create custom Kafka Connect image with YugabyteDB connector

To create a Kafka Connect image with the YugabyteDB connector, start with the [Confluent Server Docker Image for Kafka Connect](https://hub.docker.com/r/confluentinc/cp-server-connect/).

1. Create a directory which will be used to store all related files.

    ```sh
    mkdir kafka-connect-ccloud && cd kafka-connect-ccloud
    ```

    <!-- TODO Vaibhav: Step 2 and 3 can be combined, see why is not working -->
1. Download the YugabyteDB connector jar.

    ```sh
    curl -so debezium-connector-yugabytedb-1.9.5.y.22.jar https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.22/debezium-connector-yugabytedb-1.9.5.y.22.jar
    ```

1. Create a `Dockerfile` with the following contents:

    ```Dockerfile
    FROM confluentinc/cp-server-connect:7.4.0
    ADD debezium-connector-yugabytedb-1.9.5.y.22.jar /usr/share/java/kafka/
    USER 1001
    ```

1. To build the image, execute the following command:

    ```sh
    docker build . -t custom-connect:latest
    ```

1. Create a `docker-compose.yaml` file with the following contents:

    ```yaml
    version: '3'
    services:
      kafka-CCLOUD-BROKER-ENDPOINT.:latest
        container_name: kafka-connect-ccloud
        ports:
          - 8083:8083
        environment:
          CONNECT_LOG4J_APPENDER_STDOUT_LAYOUT_CONVERSIONPATTERN: "[%d] %p %X{connector.context}%m (%c:%L)%n"
          CONNECT_CUB_KAFKA_TIMEOUT: 300
          CONNECT_BOOTSTRAP_SERVERS: "CCLOUD-BROKER-ENDPOINT.confluent.cloud:9092"
          CONNECT_REST_ADVERTISED_HOST_NAME: 'kafka-connect-ccloud'
          CONNECT_REST_PORT: 8083
          CONNECT_GROUP_ID: kafka-connect-group-01-v04
          CONNECT_CONFIG_STORAGE_TOPIC: kafka-connect-configs
          CONNECT_OFFSET_STORAGE_TOPIC: kafka-connect-offsets
          CONNECT_STATUS_STORAGE_TOPIC: kafka-connect-status
          CONNECT_KEY_CONVERTER: org.apache.kafka.connect.json.JsonConverter
          CONNECT_VALUE_CONVERTER: org.apache.kafka.connect.json.JsonConverter
          CONNECT_LOG4J_ROOT_LOGLEVEL: 'INFO'
          CONNECT_LOG4J_LOGGERS: 'org.apache.kafka.connect.runtime.rest=WARN,org.reflections=ERROR'
          CONNECT_CONFIG_STORAGE_REPLICATION_FACTOR: '3'
          CONNECT_OFFSET_STORAGE_REPLICATION_FACTOR: '3'
          CONNECT_STATUS_STORAGE_REPLICATION_FACTOR: '3'
          CONNECT_PLUGIN_PATH: '/usr/share/java,/usr/share/confluent-hub-components/,/usr/share/java/kafka/'
          # Confluent Cloud config
          CONNECT_REQUEST_TIMEOUT_MS: "20000"
          CONNECT_RETRY_BACKOFF_MS: "500"
          CONNECT_SSL_ENDPOINT_IDENTIFICATION_ALGORITHM: "https"
          CONNECT_SASL_MECHANISM: "PLAIN"
          CONNECT_SECURITY_PROTOCOL: "SASL_SSL"
          CONNECT_SASL_JAAS_CONFIG: "org.apache.kafka.common.security.plain.PlainLoginModule required username='CCLOUD_USER' password='CCLOUD_PASSWORD';"
          #
          CONNECT_CONSUMER_SECURITY_PROTOCOL: "SASL_SSL"
          CONNECT_CONSUMER_SSL_ENDPOINT_IDENTIFICATION_ALGORITHM: "https"
          CONNECT_CONSUMER_SASL_MECHANISM: "PLAIN"
          CONNECT_CONSUMER_SASL_JAAS_CONFIG: "org.apache.kafka.common.security.plain.PlainLoginModule required username='CCLOUD_USER' password='CCLOUD_PASSWORD';"
          CONNECT_CONSUMER_REQUEST_TIMEOUT_MS: "20000"
          CONNECT_CONSUMER_RETRY_BACKOFF_MS: "500"
          #
          CONNECT_PRODUCER_SECURITY_PROTOCOL: "SASL_SSL"
          CONNECT_PRODUCER_SSL_ENDPOINT_IDENTIFICATION_ALGORITHM: "https"
          CONNECT_PRODUCER_SASL_MECHANISM: "PLAIN"
          CONNECT_PRODUCER_SASL_JAAS_CONFIG: "org.apache.kafka.common.security.plain.PlainLoginModule required username='CCLOUD_USER' password='CCLOUD_PASSWORD';"
          CONNECT_PRODUCER_REQUEST_TIMEOUT_MS: "20000"
          CONNECT_PRODUCER_RETRY_BACKOFF_MS: "500"
        command:
          - bash
          - -c
          - |
            echo "Launching Kafka Connect worker"
            /etc/confluent/docker/run &
            #
            echo "Waiting for Kafka Connect to start listening on localhost:8083 ⏳"
            while : ; do
                curl_status=$$(curl -s -o /dev/null -w %{http_code} http://localhost:8083/connectors)
                echo -e $$(date) " Kafka Connect listener HTTP state: " $$curl_status " (waiting for 200)"
                if [ $$curl_status -eq 200 ] ; then
                break
                fi
                sleep 5
            done
            #
            sleep infinity
    ```

    {{< note title="Using Schema Registry" >}}

If the configuration is such that it needs schema registry as well, then you need to add the following environment variables to the Docker compose file:

```yaml
CONNECT_KEY_CONVERTER: io.confluent.connect.avro.AvroConverter
CONNECT_KEY_CONVERTER_SCHEMA_REGISTRY_URL: "https://SCHEMA-RGISTRY-CCLOUD-ENDPOINT.confluent.cloud"
CONNECT_KEY_CONVERTER_BASIC_AUTH_CREDENTIALS_SOURCE: "USER_INFO"
CONNECT_KEY_CONVERTER_SCHEMA_REGISTRY_BASIC_AUTH_USER_INFO: "SCHEMA_REGISTRY_USER:SCHEMA_REGISTRY_PASSWORD"
CONNECT_VALUE_CONVERTER: io.confluent.connect.avro.AvroConverter
CONNECT_VALUE_CONVERTER_SCHEMA_REGISTRY_URL: "https://SCHEMA-REGISTRY-CCLOUD-ENDPOINT.confluent.cloud"
CONNECT_VALUE_CONVERTER_BASIC_AUTH_CREDENTIALS_SOURCE: "USER_INFO"
CONNECT_VALUE_CONVERTER_SCHEMA_REGISTRY_BASIC_AUTH_USER_INFO: "SCHEMA_REGISTRY_USER:SCHEMA_REGISTRY_PASSWORD"
```

    {{< /note >}}

    {{< note title="Using authentication and authorization" >}}

- To configure authentication, follow the instructions in [Configure Authentication for Confluent Platform with Ansible Playbooks](https://docs.confluent.io/ansible/current/ansible-authenticate.html).

- To configure authorization, follow the instructions in [Configure Authorization for Confluent Platform with Ansible Playbooks](https://docs.confluent.io/ansible/current/ansible-authorize.html).

    {{< /note >}}

1. Start the Kafka Connect cluster:

    ```sh
    docker-compose up
    ```

1. Deploy the connector. For more information, refer to [Deployment](../../../explore/change-data-capture/debezium-connector-yugabytedb/#deployment).
