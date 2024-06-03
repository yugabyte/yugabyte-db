---
title: Jaeger
linkTitle: Jaeger
description: Use Jaeger with YCQL API
menu:
  preview_integrations:
    identifier: jaeger
    parent: integrations-other
    weight: 571
type: docs
---

Jaeger, inspired by [Dapper](https://research.google.com/pubs/pub36356.html) and [OpenZipkin](http://zipkin.io/), is a distributed tracing system released as open source by [Uber Technologies](http://uber.github.io/). It is used for monitoring and troubleshooting microservices-based distributed systems, including:

- Distributed context propagation
- Distributed transaction monitoring
- Root cause analysis
- Service dependency analysis
- Performance / latency optimization

Jaeger can be used with various storage backends. YugabyteDB's [YCQL](../../api/ycql/) API can also be used as a storage backend for Jaeger (version 1.43.0, previous versions are unsupported).

The Jaeger example on this page uses the [HotROD](https://www.jaegertracing.io/docs/1.43/getting-started/#sample-app-hotrod) sample application to illustrate the use of YCQL as a storage backend.

Additionally, it uses Jaeger [All-in-one](https://www.jaegertracing.io/docs/1.43/deployment/#all-in-one), which is a special distribution that combines three Jaeger components, agent, collector, and query service/UI, in a single binary or container image.

{{< note title = "Note">}}

The following example is for testing purposes only. For production deployment, refer to the [Jaeger documentation](https://www.jaegertracing.io/docs/1.18/).

{{< /note >}}

## Prerequisites

To use Jaeger, ensure that you have the following:

- YugabyteDB up and running. Download and install YugabyteDB by following the steps in [Quick start](../../quick-start/).

- [Go 1.20](https://go.dev/doc/go1.20).

## Build the application

To build the application, do the following:

1. Jaeger natively supports two databases, Cassandra and Elasticsearch. So you need to set an environment variable `SPAN_STORAGE_TYPE` to specify your choice of database. For YCQL examples, set it to `cassandra` as follows:

    ```sh
    export SPAN_STORAGE_TYPE=cassandra
    ```

1. Clone the Jaeger repository.

    ```sh
    git clone git@github.com:jaegertracing/jaeger.git && cd jaeger
    ```

1. [Initialize](https://www.jaegertracing.io/docs/1.43/deployment/#schema-script) the cassandra keyspace and create the schema on YCQL using ycqlsh shell as follows:

    ```sh
    MODE=test sh ./plugin/storage/cassandra/schema/create.sh | path/to/ycqlsh
    ```

1. Verify that a keyspace `jaeger_v1_test` is created using ycqlsh as follows:

    ```sql
    ycqlsh> desc keyspaces;
    ```

    ```output
    system_auth  jaeger_v1_test  system_schema  system
    ```

1. From the Jaeger project home directory, add the `jaeger-ui` submodule as follows:

    ```sh
    git submodule update --init --recursive
    ```

1. Compile, package the UI assets in the `jaeger-ui` source code, and run Jaeger All-in-one using the following command:

    ```sh
    make run-all-in-one
    ```

    Wait for the logs to settle, then navigate to <http://localhost:16686> to access the Jaeger UI, where you can view the traces created by the application. This indicates that the Jaeger UI, collector, query, and agent are running.

## Run the application

To run the application, do the following:

1. From a new terminal, set the `SPAN_STORAGE_TYPE` environment variable again as follows:

    ```sh
    export SPAN_STORAGE_TYPE=cassandra
    ```

1. From your Jaeger project's home directory, start the HotROD application as follows:

    ```go
    go run ./examples/hotrod/main.go all
    ```

## Verify the integration

To verify the integration, navigate to the [HotROD landing page](http://localhost:8080) and click a few options on the page to verify if you get a response about a driver arriving in a few minutes. This triggers a few traces which are in turn stored in the YCQL backend storage. You can view the traces in the Jaeger UI at <http://localhost:16686>.

You can also verify the integration from your ycqlsh shell using the following commands:

    ```sql
    use jaeger_v1_test;
    select * from jaeger_v1_test.traces;
    ```

    All the traces created by the HotROD application are stored in the traces table.
