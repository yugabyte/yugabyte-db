Go to the `Configuration` nav on the left-side and then click on the GCP tab. You should see
something like this:

![PKS Configuration -- empty](/images/ee/pks-setup/pks-configure-empty.png)

Select Pivotal Container Service from the Type dropdown,
Give a meaningful name for your config.

Service Account, provide the name of the service account which has necessary access to manage
the cluster, refer to [Create Service Account](/deploy/kubernetes/helm-chart/#create-service-account).

Fill in the couple of pieces of data and you should get something like:

![PKS Configuration -- filled](/images/ee/pks-setup/pks-configure-filled.png)

Click on Add Region and fill the zones
![PKS Configuration -- region](/images/ee/k8s-setup/k8s-add-region-flow.png)

Hit Save to save the configuration
