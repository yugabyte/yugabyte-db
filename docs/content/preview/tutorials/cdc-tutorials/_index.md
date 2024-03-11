---
title: Change data capture environment tutorials
headerTitle: Kafka environments
linkTitle: Kafka environments
description: Use YugabyteDB CDC to stream data with different Kafka environments such as Amazon MSK, Event Hubs, Confluent Cloud, and more.
headcontent: Use YugabyteDB CDC to stream data with different Kafka environments
image: /images/section_icons/develop/ecosystem/apache-kafka-icon.png
aliases:
  - /preview/integrations/apache-kafka/
cascade:
  earlyAccess: /preview/releases/versioning/#feature-availability
menu:
  preview_tutorials:
    identifier: tutorials-kafka-stream
    parent: tutorials-cdc
type: indexpage
---

{{<index/block>}}

  {{<index/item
    title="Amazon MSK"
    body="Configure and stream data into Amazon MSK using Debezium connector."
    href="cdc-aws-msk/"
    icon="/images/section_icons/develop/ecosystem/amazon-msk.png">}}

  {{<index/item
    title="Azure Event Hubs"
    body="Connect to Azure Event Hubs and ingest data into Azure Synapse Analytics for analysis."
    href="cdc-azure-event-hub/"
    icon="/images/section_icons/develop/ecosystem/azure-event-hub.png">}}

  {{<index/item
    title="Confluent Cloud"
    body="Use YugabyteDB connector to publish changes to Confluent Cloud."
    href="cdc-confluent-cloud/"
    icon="/images/section_icons/develop/ecosystem/confluent-cloud.jpg">}}

  {{<index/item
    title="Redpanda"
    body="Use YugabyteDB CDC with Redpanda as message broker."
    href="cdc-redpanda/"
    icon="/images/section_icons/quick_start/sample_apps.png">}}

{{</index/block>}}
