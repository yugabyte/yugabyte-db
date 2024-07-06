---
title: Scrape metrics from universe nodes
headerTitle: Scrape metrics from universe nodes
linkTitle: Scrape nodes
description: Export universe metrics to your own Prometheus instance
headcontent: Scrape from universe nodes directly
menu:
  stable_yugabyte-platform:
    parent: prometheus-custom
    identifier: prometheus-scrape
    weight: 20
type: docs
---

Your independent Prometheus instance will scrape data from database nodes directly (in the case of VM-based universes) or scrape data from a Kubernetes Prometheus Operator Service Monitor (in the case of K8s-based universes). This data scraping runs in parallel to and independently from the YugabyteDB Anywhere-embedded Prometheus.

{{< tabpane text=true >}}

  {{% tab header="Cloud or on-premises universe" lang="Virtual machines" %}}

Every node of a YugabyteDB universe exports granular time series metrics. The metrics are formatted in both Prometheus exposition format or JSON for seamless integration with Prometheus.

**Prometheus format**

View YB-TServer metrics in Prometheus format in the browser or via the CLI using the following command:

```sh
curl <node IP>:9000/prometheus-metrics
```

View YB-Master server metrics in Prometheus format in the browser or via the CLI using the following command:

```sh
curl <node IP>:7000/prometheus-metrics
```

**JSON format**

The YugabyteDB Anywhere API can expose the health check results as a JSON blob. You can view YB-TServer metrics in JSON format in the browser or via the CLI using the following command:

```sh
curl <node IP>:9000/metrics
```

Using this API to retrieve health check alerts would require you to first parse the JSON, and then do some text parsing afterward to scrape the metrics from each field.

  {{% /tab %}}

  {{% tab header="Kubernetes universe" lang="Kubernetes universe" %}}

The [Prometheus Operator](https://prometheus-operator.dev) should be installed on your Kubernetes universe. To verify that it is running, use the following command:

```sh
kubectl get pods -n kube-prometheus-stack kube-prometheus-stack-operator-5577f9747-hqzbw
```

```output
NAME                                             READY   STATUS    RESTARTS   AGE
kube-prometheus-stack-operator-5577f9747-hqzbw   1/1     Running   0          4d19h
```

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

    This enables a service monitor and adds an extra label `release: prom`, which is required by the Prometheus Operator to discover the service monitor.

1. Click **Upgrade**.

To verify the configuration:

1. Access your Prometheus dashboard.

1. Navigate to the **Service Discovery** page and search for the name of your universe.

    The service monitor for your YugabyteDB universe should be listed.

    ![Prometheus Service Discovery](/images/yp/alerts-monitoring/prometheus-service-discovery.png)

To view metrics, use Prometheus queries. For sample queries, refer to [Analyze key metrics](../../../explore/observability/prometheus-integration/#analyze-key-metrics).

If you have [Grafana](../../../explore/observability/grafana-dashboard/grafana/) available, you can access a rich set of visualizations using the [YugabyteDB Grafana dashboard](https://grafana.com/grafana/dashboards/12620-yugabytedb/).

  {{% /tab %}}

{{< /tabpane >}}
