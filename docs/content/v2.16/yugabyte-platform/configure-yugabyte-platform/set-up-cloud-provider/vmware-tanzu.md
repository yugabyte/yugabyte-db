---
title: Configure the VMware Tanzu cloud provider
headerTitle: Configure the VMware Tanzu cloud provider
linkTitle: Configure cloud providers
description: Configure the VMware Tanzu cloud provider
menu:
  v2.16_yugabyte-platform:
    identifier: set-up-cloud-provider-4-vmware-tanzu
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu/" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

<br>You can configure VMware Tanzu Kubernetes Grid (TKG) for a YugabyteDB universe using YugabyteDB Anywhere.

Before you start, ensure that you have the `kubeconfig` file generated during [YugabyteDB Anywhere Installation](/preview/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes/#create-a-kubeconfig-file-for-a-kubernetes-cluster) so YugabyteDB Anywhere can use the provided credentials to automatically provision and deprovision Kubernetes pods that run the YugabyteDB universe.

To start configuring any TKG edition (that is, either TKG-Integrated, TKG-Service, or TKG-Multicloud), open the YugabyteDB Anywhere UI, navigate to **Dashboard**, and click **Configure a Provider**.

## Configure TKG credentials

You configure the TKG credentials as follows:

- Navigate to **Configs > Infrastructure > VMware Tanzu**, as per the following illustration:<br>

  ![Tanzu Configuration](/images/deploy/pivotal-cloud-foundry/tanzu-config-1.png)

- Use the **Name** field to provide a meaningful name for your configuration.

- Use the **Kube Config** field to specify the kube config for an availability zone at one of the following levels:

  - At the **provider level**, in which case this configuration file will be used for all availability zones in all regions. You use the **Cloud Provider Configuration** window for this setting.
  - At the **zone level**, which is important for multi-zone or multi-region deployments. You use the **Add new region** dialog for this setting.

- Use the **Service Account** field to provide the name of the service account that has the necessary access to manage the cluster, as described in [Create cluster](/preview/deploy/kubernetes/single-zone/oss/helm-chart/#create-cluster).

- Use the **Image Registry** field to specify the location of the YugabyteDB image. You should accept the default setting, unless you are hosting your own registry.

- The **Pull Secret File** field indicates that the Enterprise YugabyteDB image is in a private repository. Use this field to upload the pull secret for downloading the images. The secret should be supplied by your organization's sales team.


## Configure region and zones

You configure region and zones as follows:

- On the **Create VMware Tanzu Configuration** page, click **Add region** to open the **Add new region** dialog.

- Use the **Region** field to select the region.

- Complete the fields of the expanded **Add new region** dialog shown in the following illustration:

  <br>

  ![Add Region](/images/deploy/pivotal-cloud-foundry/add-region-1.png)<br>

  - Use the **Zone** field to enter a zone label that matches your failure domain zone label `failure-domain.beta.kubernetes.io/zone`

  - In the **Storage Class** field, provide the storage class that exists in your Kubernetes cluster and matches the one installed on TKG. The valid input is a comma delimited value. The default is standard. That is, the default storage class is `TKG - Multi Cloud: standard-sc`, `TKG - Service: tkg-vsan-storage-policy`.

  - In the **Namespace** field, specify an existing namespace into which pods in this zone will be deployed.

  - In the **Cluster DNS Domain** field, provide the DNS domain name used in the Kubernetes cluster.

  - Use the **Kube Config** field to upload the kube config file.

  - Optionally, complete the **Overrides** field. If not completed, YugabyteDB Anywhere uses the default values specified inside the Helm chart.

    To add service-level annotations, use the following overrides:

    ```config
    serviceEndpoints:
      - name: "yb-master-service"
        type: "LoadBalancer"
        annotations:
          service.beta.kubernetes.io/aws-load-balancer-internal: "0.0.0.0/0"
        app: "yb-master"
        ports:
          ui: "7000"

      - name: "yb-tserver-service"
        type: "LoadBalancer"
        annotations:
          service.beta.kubernetes.io/aws-load-balancer-internal: "0.0.0.0/0"
        app: "yb-tserver"
        ports:
          ycql-port: "9042"
          yedis-port: "6379"
          ysql-port: "5433"
    ```

    <br>To disable LoadBalancer, use the following overrides:

    ```configuration
    enableLoadBalancer: False
    ```

    To change the cluster domain name, use the following overrides:

    ```configuration
    domainName: my.cluster
    ```

    To add annotations at the StatefulSet level, use the following overrides:

    ```configuration
    networkAnnotation:
      annotation1: 'foo'
      annotation2: 'bar'
    ```

  - If required, add a new zone by clicking **Add Zone**, as your configuration may have multiple zones.

  - Click **Add Region**.

  - Click **Save**. <br>

  If your configuration is successful, you are redirected to **VMware Tanzu configs**.

## Appendix: VMware Tanzu application service

VMware Tanzu Application Service is no longer actively supported and the following information is considered legacy.

If you choose to use VMware Tanzu Application Service, before creating the service instance, ensure that the following is available:

- The YugabyteDB tile is installed in your PCF marketplace.
- The cloud provider is configured in the YugabyteDB Anywhere instance in your PCF environment .

### Create a YugabyteDB service instance

You can create a YugabyteDB service instance via the App Manager UI or Cloud Foundry (cf) command-line interface (CLI).

#### Use the PCF app manager

- In your PCF App manager, navigate to the marketplace and select **YugabyteDB**.
- Read descriptions of the available service plans to identify the resource requirements and intended environment, as shown in the following illustration:<br>

  ![Yugabyte Service Plans](/images/deploy/pivotal-cloud-foundry/service-plan-choices.png)

- Select the service plan.
- Complete the service instance configuration, as shown in the following illustration:<br>

  ![App Manager Config](/images/deploy/pivotal-cloud-foundry/apps-manager-config.png)

#### Use the cloud foundry CLI

You can view the marketplace and plan description in the cf CLI by executing the following command:

```sh
cf marketplace -s yugabyte-db
```

The output should be similar to the following:

```output
service plan   description                  free or paid
x-small        Cores: 2, Memory (GB): 4     paid
small          Cores: 4, Memory (GB): 7     paid
medium         Cores: 8, Memory (GB): 15    paid
large          Cores: 16, Memory (GB): 15   paid
x-large        Cores: 32, Memory (GB): 30   paid
```

Once you decide on the service plan, you can launch the YugabyteDB service instance by executing the following command:

```sh
cf create-service yugabyte-db x-small yb-demo -c '{"universe_name": "yb-demo"}'
```

### Configure the YugabyteDB service instance

You can specify override options when you create a service instance using the YugabyteDB service broker.

#### Override cloud providers

Depending on the cloud providers configured for your YugabyteDB Anywhere, you can create Yugabyte service instances by providing overrides.

To provision in AWS or GCP cloud, your overrides should include the appropriate `provider_type` and `region_codes` as an array, as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "provider_type": "gcp", # gcp for Google Cloud, aws for Amazon Web Service
 "region_codes": ["us-west1"] # comma delimited list of regions
}
```

To provision in Kubernetes, your overrides should include the appropriate `provider_type` and `kube_provider` type, as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "provider_type": "kubernetes",
 "kube_provider": "gke" # gke for Google Compute Engine, pks for Pivotal Container Service (default)
}
```

#### Override the number of nodes

To override the number of nodes, include the `num_nodes` with the desired value, and then include this parameter along with other parameters for the cloud provider, as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "num_nodes": 4 # default is 3 nodes.
}
```

#### Override the replication factor

To override the replication factor, include `replication` with the desired value, and then include this parameter along with other parameters for the cloud provider, as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "replication": 5,
 "num_nodes": 5 # if the replication factor is 5, num_nodes must be 5 minimum
}
```

*replication* must be set to 1, 3, 5, or 7.

#### Override the volume settings

To override the volume settings, include `num_volumes` with the desired value, as well as `volume_size` with the volume size in GB for each of those volumes. For example, to have two volumes with 100GB each, overrides should be specified as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "num_volumes": 2,
 "volume_size": 100
}
```

#### Override  the YugabyteDB software version

To override the YugabyteDB software version to be used, include `yb_version` with the desired value, ensuring that this version exists in YugabyteDB Anywhere, as follows:

```configuration
{
 "universe_name": "cloud-override-demo",
 "yb_version": "1.1.6.0-b4"
}
```
