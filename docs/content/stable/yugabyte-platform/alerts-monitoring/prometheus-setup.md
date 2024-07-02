---
title: Use your own Prometheus instance
headerTitle: Use a custom Prometheus instance
linkTitle: Custom Prometheus
description: Export universe metrics to your own Prometheus instance
headcontent: Export universe metrics to your own Prometheus instance
menu:
  stable_yugabyte-platform:
    parent: alerts-monitoring
    identifier: prometheus-setup
    weight: 90
type: docs
---

YugabyteDB Anywhere provides [metrics](../anywhere-metrics/) via a Prometheus instance that is created with your YugabyteDB Anywhere installation.

You can also set up your own Prometheus instance and use it to scrape universe metrics, and pass the metrics for visualization in Grafana or any other tool that supports Prometheus as a data source.

## Kubernetes

YugabyteDB allows scraping of universe metrics in Kubernetes using a ServiceMonitor. Similar to a Pod or Deployment, ServiceMonitor is also a resource that is handled by the Prometheus Operator. The ServiceMonitor resource selects a Kubernetes Service based on a given set of labels. With the help of this selected Service, the Prometheus Operator creates scrape configurations for Prometheus. Prometheus uses these configurations to actually scrape the application pods.

The ServiceMonitor resource can have details like port names or numbers, scrape interval, HTTP path where application is exposing the metrics, and so on. In the case of the `yugabytedb/yugabyte` Helm chart, it creates ServiceMonitors for YB-Master as well as YB-TServer pods.

### Prerequisites

Install the Prometheus Operator on your Kubernetes universe as follows:

```sh
kubectl get pods -n kube-prometheus-stack kube-prometheus-stack-operator-5577f9747-hqzbw
```

```output
NAME                                             READY   STATUS    RESTARTS   AGE
kube-prometheus-stack-operator-5577f9747-hqzbw   1/1     Running   0          4d19h
```

### Use Prometheus with a YugabyteDB Anywhere Kubernetes universe

To use a custom Prometheus instance with a universe on Kubernetes:

1. In YugabyteDB Anywhere, navigate to the universe you want to monitor.

1. Click **Actions** and choose **Edit Kubernetes Overrides**.

1. Add the following configuration:

    ```yaml
    serviceMonitor:
      enabled: true
      extraLabels:
        release: prom
    ```

    This configuration enables the service monitor and adds an extra label `release: prom`, which is required by the Prometheus Operator to discover this service monitor.

1. Click **Upgrade**.

To verify the configuration:

1. Access your Prometheus dashboard.

1. Navigate to the **Service Discovery** page.

1. Ensure that the service monitor for your YugabyteDB universe is listed.

To view metrics, use Prometheus queries. For sample queries, refer to [Analyze key metrics](../../../explore/observability/prometheus-integration/macos/#analyze-key-metrics)

If you have [Grafana](../../../explore/observability/grafana-dashboard/grafana/) available, you can access a rich set of visualizations using the [YugabyteDB Grafana dashboard](https://grafana.com/grafana/dashboards/12620-yugabytedb/).
