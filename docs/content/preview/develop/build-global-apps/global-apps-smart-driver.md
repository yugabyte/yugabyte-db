---
title: Automatic load balancing and failover with Smart Driver
headerTitle: Automatic load balancing and failover of global applications
linkTitle: 4. Load balancing patterns
description: Automatic load balancing and failover
headcontent: Learn how to load balance and failover with Smart Driver
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-smartdriver
    weight: 2040
type: docs
---

The [Smart Driver for YSQL](../../../drivers-orms/smart-drivers/) provides advanced cluster-aware load-balancing capabilities that can greatly simplify your application and infrastructure. Typically, to load balance across a set of nodes you would need a separate load balancer through which all requests have to be routed as shown in the illustration.

![Cluster-aware load balancer](/images/develop/global-apps/no-smart-driver.png)

## Cluster-aware load-balancing

With [YugabyteDB YSQL Smart Drivers](../../../drivers-orms/smart-drivers/), available in multiple languages, you do not need a separate load balancer service. The smart driver needs the IP/hostname of just one node in the cluster.

It automatically fetches the complete node list from the cluster and spreads the connections from your applications to various nodes in the cluster. It also refreshes the node list regularly (default: 5 minutes). This can be activated by passing the `load_balance` option in your connection string like

```python
cxnstr = "postgres://username:password@<node-ip>:5433/database_name?load_balance=true"
```

{{<note title="Note" >}}
It is a good practice to pass in a few more nodes in the connection string from different zones/regions to handle connectivity issues during the initial connection. For example, `postgres://node1,node2,node3/ ...`
{{</note>}}

If you've set preferred leaders, you can use the `topology_keys` option to send connections only to nodes in that region. For example, if you had set us-central as your preferred region, then adding the following `topology_keys` would make sure the client connects only to the nodes in us-central.

```python
cxnstr = "load_balance=true&topology_keys=aws.us-central.*"
```

![Cluster-aware load balancing](/images/develop/global-apps/smart-driver-loadbalance.png)

{{<tip>}}
For more details, see [Smart load-balancing](../../../drivers-orms/smart-drivers/#topology-aware-connection-load-balancing)
{{</tip>}}

## Cluster-aware failover

With [YugabyteDB YSQL Smart Drivers](../../../drivers-orms/smart-drivers/) you can also set a fallback hierarchy by assigning priority to ensure that connections are made to the region with the highest priority, and then fall back to the region with the next priority in case the high-priority region fails. For example:

```python
cxnstr = "load_balance=true&topology_keys=aws.us-central.*:1,aws.us-west.*:2"
```

This ensures that initial connections are made to `us-central`. In case `us-central` fails, only then will connections will be automatically made to `us-west`.

![Cluster-aware failover](/images/develop/global-apps/smart-driver-failover.png)

{{<tip>}}
For more details, see [Fallback topology keys](../../../drivers-orms/smart-drivers/#fallback-topology-keys)
{{</tip>}}

## DNS Load-balancing

[DNS](https://en.wikipedia.org/wiki/Domain_Name_System) servers maintain the mapping of host names to ip addresses. Every time you enter a URL on a browser or ssh to a host, applications such as browsers and ssh clients access the DNS servers to convert the hostnames to IP addresses. An interesting part about the mapping is that one hostname can map to many IP addresses. And the DNS server returns one of the many IP addresses attached to a hostname every time you query it. Typically websites use this for load-balancing their servers.

You can use the same principle for your YugabyteDB cluster. You can configure a hostname(eg. `ybcluster.mycompany.com`) for your YugabyteDB cluster and map all the YugabyteDB node IP addresses to it. This way any application tries to connect to `ybcluster.mycompany.com`, your DNS resolver will resolve to one of the node-ips in your cluster.

For example, let's say you have a local three-node cluster with nodes running on `127.0.0.1`, `127.0.0.2`, and `127.0.0.3`. If you use [dnsmasq](https://thekelleys.org.uk/dnsmasq/doc.html), you can add the following configuration:

```dns
address=/ybcluster.mycompany.com/127.0.0.1
address=/ybcluster.mycompany.com/127.0.0.2
address=/ybcluster.mycompany.com/127.0.0.3
```

Now, if you fetch the DNS info via `dig`:

```bash
dig @localhost ybcluster.mycompany.com
```

you will an output similar to:

```dns
;; ANSWER SECTION:
ybcluster.mycompany.com. 0  IN  A  127.0.0.3
ybcluster.mycompany.com. 0  IN  A  127.0.0.2
ybcluster.mycompany.com. 0  IN  A  127.0.0.1
```

DNS servers typically will default to a round-robin policy when multiple IP addresses are attached to the same hostname. When a client application connects to your YugabyteDB cluster, it will get a different IP address every time thereby automatically load-balancing your cluster. One problem with this is that the local resolver client on a machine could cache the last resolution for a while. During this period all applications on a machine would end up connecting to the same node.

{{<tip>}}
You can configure this on your cloud provider's DNS like [AWS Route 53](https://aws.amazon.com/route53/), [Azure DNS](https://azure.microsoft.com/en-us/products/dns/) or [GCP DNS](https://cloud.google.com/dns)
{{</tip>}}

## NLB Load-balancing

[Network load-balancers(NLBs)](https://en.wikipedia.org/wiki/Network_load_balancing) work similarly to DNS load-balancing but there are many advantages.

1. NLBs use advanced routing mechanisms and health metrics to route your connection to the best node.
1. NLBs have their own IP addresses which are different from the IP addresses of the nodes behind them. So applications do not have to worry about caching of the results(like DNS caching) as the resolved IP would be the NLB's IP and not the node IP.
1. As the DB nodes hide behind the NLB, they are not directly exposed to the world providing additional security to your systems.

One caveat is that even though NLBs are fast (can handle millions of requests per second), they sit in between the client and the DB nodes. This is an additional hop for your request to get to the DB nodes. Depending on the performance needs of your application, this may be a concern.

{{<tip>}}
Most cloud providers offer NLB as a service to their users. See [Elastic Load balancer](https://aws.amazon.com/elasticloadbalancing/network-load-balancer/), [GCP Cloud Load Balancer](https://cloud.google.com/load-balancing) or [Azure Load Balancer](https://learn.microsoft.com/en-us/azure/load-balancer/load-balancer-overview).
{{</tip>}}

## Kubernetes-based global apps

[Kubernetes](https://kubernetes.io/) is designed to run across [multiple zones within a region](https://kubernetes.io/docs/setup/best-practices/multiple-zones/). For global applications, YugabyteDB needs to be deployed across multiple Kubernetes clusters located in different regions. For example, you can deploy a three-region YugabyteDB cluster on three Kubernetes clusters, each deployed in a different region, using the standard single-zone [YugabyteDB Helm chart](https://artifacthub.io/packages/helm/yugabyte/yugabyte) to deploy one-third of the nodes in the database cluster in each of the three clusters.

On kubernetes, YugabytedDB t-servers and master servers are modeled as [StatefulSets](https://kubernetes.io/docs/concepts/workloads/controllers/statefulset/) so that the pods get presistent storage access and have stable network names/ids across restarts.


{{<tip>}}
For more information, see  [Deploying on GKE](../../../deploy/kubernetes/multi-cluster/gke/helm-chart/)
{{</tip>}}