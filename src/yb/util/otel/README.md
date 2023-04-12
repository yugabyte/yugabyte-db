# Instructions to compile and setup Otel

## Compile

Checkout `otel2` branch in `vrajat/yugabyte-db-thirdparty` and compile it.

```
cd code
git clone git@github.com:vrajat/yugabyte-db-thirdparty
git checkout otel2
./build_thirdparty.sh --toolchain=llvm15
```

Checkout `otel` branch in `vrajat/yugabyte-db` and compile it.

```
cd code
git clone git@github.com:vrajat/yugabyte-db
git checkout otel
YB_THIRDPARTY_DIR=~/code/yugabyte-db-thirdparty ./yb_build.sh
```

## Export traces to files.

By default, traces are exported to files by PG backend and tservers.

```
./bin/yb-ctl create --rf=1 --tserver_flags="enable_otel_tracing=true"
# ...
# < Run Queries>
# ...
```

Traces are exported to files with name PG-<trace id>.log and TSERVER-<trace id>.log

```
ls -l ~/yugabyte-data/node-1/disk-1/yb-data/tserver/logs/

-rw------- 1 centos centos   7820 Apr 12 13:14 PG-a07edddc5c6143f3ff997848327af137.log
-rw------- 1 centos centos  36920 Apr 12 13:14 PG-f39fa0d446dcbaaa940537034c2dbc29.log
-rw-rw-r-- 1 centos centos   5148 Apr 12 13:14 TSERVER-a07edddc5c6143f3ff997848327af137.log
-rw-rw-r-- 1 centos centos  33177 Apr 12 13:14 TSERVER-f39fa0d446dcbaaa940537034c2dbc29.log
```

Trace files with the same trace id belong to the same query.

## Export to Otel Collector and Jaeger.

### Setup Otel Collector and Jaeger

Copy the docker compose and otel config file.


docker-compose.yaml

```
version: '3.7'
services:
  otel-collector:
    image: otel/opentelemetry-collector
    command: [--config=/etc/otel-collector-config.yaml]
    volumes:
      - ./otel-collector-config.yaml:/etc/otel-collector-config.yaml
    ports:
      - 1888:1888 # pprof extension
      - 8888:8888 # Prometheus metrics exposed by the collector
      - 8889:8889 # Prometheus exporter metrics
      - 13133:13133 # health_check extension
      - 4317:4317 # OTLP gRPC receiver
      - 4318:4318 # OTLP http receiver
      - 55679:55679 # zpages extension
  jaeger:
    image: jaegertracing/all-in-one:latest
    ports:
      - "16686:16686"
      - "14268:14268"
      - "14250:14250"
    environment:
      - COLLECTOR_OTLP_ENABLED=true
      - LOG_LEVEL=info
```

otel-collector-config.yaml

```
receivers:
  otlp:
    protocols:
      grpc:
      http:

processors:
  batch:

exporters:
  logging:
   loglevel: info
  jaeger:
      endpoint: jaeger:14250
      tls:
        insecure: true

service:
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch]
      exporters: [jaeger, logging]
```

Start the docker containers.

```
docker compose up -d
```

Start Yugabyte cluster


```
./bin/yb-ctl create --rf=1 --tserver_flags="enable_otel_tracing=true,otel_export_collector=true,otel_collector_hostname=127.0.0.1"
```