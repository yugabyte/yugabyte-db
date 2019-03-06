---
title: 4. Run Sample Apps
linkTitle: 4. Run Sample Apps
description: Run Sample Apps
aliases:
  - /quick-start/run-sample-apps/
menu:
  latest:
    parent: quick-start
    weight: 150
---

After [creating a local cluster](../create-local-cluster/), follow the instructions below to run [Yugastore](https://github.com/YugaByte/yugastore), one of simplest sample apps we have created for YugaByte DB. Yugastore is a fully open source, online bookstore app built using React, Express, NodeJS and is powered by the YCQL and YEDIS APIs of YugaByte DB. Enhancements to add YSQL API access from Yugastore is currently in the works. You can review Yugastore's architecture and data model [here](../../develop/realworld-apps/ecommerce-app/).

After running Yugastore, we recommend running the [IoT Fleet Management](../../develop/realworld-apps/iot-spark-kafka-ksql/) app. This app is built on top of YugaByte DB as the database (using the YCQL API), Confluent Kafka as the message broker, KSQL or Apache Spark Streaming for real-time analytics and Spring Boot as the application framework.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker"></i>
      Docker
    </a>
  </li>
  <li >
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/run-sample-apps.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/run-sample-apps.md" /%}}
  </div>
   <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/run-sample-apps.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/run-sample-apps.md" /%}}
  </div>
</div>