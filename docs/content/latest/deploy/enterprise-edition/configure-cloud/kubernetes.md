Go to the `Configuration` nav on the left-side and then click on the GCP tab. You should see
something like this:

<img title="K8s Configuration -- empty" alt="K8s Configuration -- empty" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-empty.png" />

Select the Kubernetes provider type from the Type dropdown.
Give a meaningful name for your config.

Service Account, provide the name of the service account which has necessary access to manage
the cluster, refer to [Create Service Account](/deploy/kubernetes/helm-chart/#create-service-account).

Kube Config, there are two ways to specify the kube config for an Availability Zone. One is through the provider form as shown above. If specified, this config file will be used for all AZ's in all regions. The other way is to specify the kube config at the zone level as shown in the screenshot below.

<img title="K8s Configuration -- zone config" alt="K8s Configuration -- zone config" class="expandable-image" src="/images/ee/k8s-setup/k8s-az-kubeconfig.png" />

Image Registry allows you to pull a Docker image from an image registry like [Quay.io](https://quay.io/).

Pull Secret, a secret token file to allow YugaByte to pull the container image from an image registry.

Fill in the couple of pieces of data and you should get something like:

<img title="K8s Configuration -- filled" alt="K8s Configuration -- filled" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-filled.png" />

Click on Add Region to open the modal to add zones for a new region.

Specify a Region and the dialog will expand to show the zone form.

You are required to add a Zone label for each AZ.

Storage Classes allow you to describe the class of storage. This field accepts comma-delimited values and defaults to `standard` if left blank.

The zone-level Kube Config allows you to specify a config and override the global Kube Config described earlier.

Overrides, a YAML configuration where you can pass in custom annotations to override default values for internal load balancers. An example is shown here:

```
serviceEndpoints:
  - name: "yb-master-service"
    type: "LoadBalancer"
    annotations:
        service.beta.kubernetes.io/aws-load-balancer-internal: 0.0.0.0/0
    app: "yb-master"
    ports:
      ui: "7000"

  - name: "yb-tserver-service"
    type: "LoadBalancer"
    annotations:
        service.beta.kubernetes.io/aws-load-balancer-internal: 0.0.0.0/0
    app: "yb-tserver"
    ports:
      ycql-port: "9042"
      yedis-port: "6379"
      ysql-port: "5433"
```

Add a new AZ by clicking on Add Zone button on the bottom left of the zone form.

Your form may have multiple AZ's as shown below.

<img title="K8s Configuration -- region" alt="K8s Configuration -- region" class="expandable-image" src="/images/ee/k8s-setup/k8s-add-region-flow.png" />

Click Add Region to add the region and close the modal.

Hit Save to save the configuration. If successful, it will redirect you to the table view of all configs.
