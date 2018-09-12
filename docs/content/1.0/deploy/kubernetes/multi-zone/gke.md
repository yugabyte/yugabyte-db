## 1. Prerequisites

Turn on the beta API.

```{.sh .copy .separator-dollar}
$ gcloud config set container/use_v1_api false
```
```
Updated property [container/use_v1_api].
```

Unset the default zone. This will interfere with various commands.

```{.sh .copy .separator-dollar}
$ gcloud config unset compute/zone
```
```
Unset property [compute/zone].
```

You can verify that the zone is unset as follows.

```{.sh .copy .separator-dollar}
$ gcloud config get-value compute/zone
```
```
(unset)
```

## 2. Create a regional cluster

Choose a region that has at least 3 zones in it. We will deploy a multi-zone YugaByte DB cluster in `us-west1` region in this tutorial. To list the various regions and the zones in them run the following command.

```{.sh .copy .separator-dollar}
$ gcloud compute zones list
```
```
NAME                       REGION                   STATUS
...
us-west1-b                 us-west1                 UP
us-west1-c                 us-west1                 UP
us-west1-a                 us-west1                 UP
...
```


Create the multi-zone regional cluster. Remember to set --num-nodes to 1 since we just need one node per region. We are just creating the Kubernetes master in this configuration, each node will have one core. We will deploy a separate node pool for the YugaByte machines. This is to allow the application to be deployed in a separate node pool in the same GKE kubernetes cluster.

```{.sh .copy .separator-dollar}
$ gcloud beta container clusters create yugabyte-us-west1 --region us-west1 --num-nodes 1
```
```
NAME               LOCATION  MASTER_VERSION  MASTER_IP      MACHINE_TYPE   NODE_VERSION  NUM_NODES  STATUS
yugabyte-us-west1  us-west1  1.8.8-gke.0     35.227.159.71  n1-standard-1  1.8.8-gke.0   3          RUNNING
```


## 3. Create the node pool

Create a nood-pool with the desired spec.

```{.sh .copy .separator-dollar}
$ gcloud beta container node-pools create node-pool-multi-zone-yb \
    --cluster=yugabyte-us-west1 \
    --local-ssd-count=2 \
    --machine-type=n1-standard-8 \
    --num-nodes=1 \
    --region=us-west1
```
```
NAME                     MACHINE_TYPE   DISK_SIZE_GB  NODE_VERSION
node-pool-multi-zone-yb  n1-standard-8  100           1.8.8-gke.0
```


Select the labels for the nodes with local SSDs. You should see labels showing their region and zone. As shown below, we will setup the YugaByte DB cluster in region `us-west1` across zones `us-west1-a`, `us-west1-b` and `us-west1-c`.

```{.sh .copy .separator-dollar}
$ kubectl get nodes --selector cloud.google.com/gke-local-ssd=true --show-labels
```
```
NAME                                                 STATUS          LABELS
gke-yugabyte-us-west-node-pool-multi-16a2cdc0-wzcv   Ready     ...   failure-domain.beta.kubernetes.io/region=us-west1,failure-domain.beta.kubernetes.io/zone=us-west1-c
gke-yugabyte-us-west-node-pool-multi-8babe56e-hb8v   Ready     ...   failure-domain.beta.kubernetes.io/region=us-west1,failure-domain.beta.kubernetes.io/zone=us-west1-b
gke-yugabyte-us-west-node-pool-multi-bd870d0e-651w   Ready     ...   failure-domain.beta.kubernetes.io/region=us-west1,failure-domain.beta.kubernetes.io/zone=us-west1-a
```


## 4. Label the nodes

Since `gcloud` based node labels are not automatically passed to `kubectl`, we need to label the nodes explicitly. We will select all the nodes in each zone and label them.

Let us first verify the selection criteria for the nodes for zone `us-west1-a`. The node selectors we will be using to place pods are:
- `cloud.google.com/gke-local-ssd` being `true` to select nodes with local ssd)
- `failure-domain.beta.kubernetes.io/region` being `us-west1` to select nodes in the appropriate region
- `failure-domain.beta.kubernetes.io/zone` being `us-west1-a` to select nodes in the first zone.

```{.sh .copy .separator-dollar}
$ kubectl get nodes -l cloud.google.com/gke-local-ssd=true,failure-domain.beta.kubernetes.io/zone=us-west1-a
```
```
NAME                                                  STATUS    ROLES     AGE       VERSION
gke-yugabyte-us-west-node-pool-multi--bd870d0e-651w   Ready     <none>    1h        v1.8.8-gke.0
```

We can extract just the node name by adding a `-o name` to the command. Run the following to label the nodes in `us-west1-a`. We will repeat this step for zones `us-west1-b` and `us-west1-c`.

```{.sh .copy .separator-dollar}
$ kubectl get nodes -l cloud.google.com/gke-local-ssd=true,failure-domain.beta.kubernetes.io/zone=us-west1-a -o name | sed 's/nodes\///'
```
```
gke-yugabyte-us-west-node-pool-multi--bd870d0e-651w
```

Add the `placement.cloud`, `placement.region` and `placement.zone` label to these nodes.

```{.sh .copy .separator-dollar}
kubectl label nodes gke-yugabyte-us-west-node-pool-multi--bd870d0e-651w placement.cloud=gcp placement.region=us-west1 placement.zone=us-west1-a
```
```
node "gke-yugabyte-us-west-node-pool-multi--bd870d0e-651w" labeled
```




## 5. Create the pods in each of the zones with the appropriate config

### Bring up the pods in zone `us-west1-a`

Create a config map with the first placement zone `us-west1-a`. Delete it first if necessary.

```{.sh .copy .separator-dollar}
$ kubectl delete configmap placement-info
```
```
configmap "placement-info" deleted
```


```{.sh .copy .separator-dollar}
$ kubectl create configmap placement-info --from-literal=placement.cloud=gcp \
                                          --from-literal=placement.region=uswest1 \
                                          --from-literal=placement.zone=us-west1-a
```
```
configmap "placement-info" created
```

You can check the config map by doing the following.

```{.sh .copy .separator-dollar}
$ kubectl get configmaps placement-info -o yaml
```
```
apiVersion: v1
data:
  placement.cloud: gcp
  placement.region: uswest1
  placement.zone: us-west1-b
kind: ConfigMap
metadata:
  creationTimestamp: 2018-04-09T00:03:26Z
  name: placement-info
  namespace: default
  ...
```


```{.sh .copy .separator-dollar}
kubectl apply -f yugabyte-statefulset-multi-zone-gke.yaml
```


## 6. Delete the cluster (optional)

To delete the cluster, run the following command:

```{.sh .copy .separator-dollar}
$ gcloud beta container clusters delete yugabyte-us-west1 --region us-west1
```


- Install Helm Charts

Download the package. Latest version is here: https://github.com/kubernetes/helm/releases

```{.sh .copy .separator-dollar}
$ wget https://storage.googleapis.com/kubernetes-helm/helm-v2.9.0-rc3-linux-amd64.tar.gz
```

Untar

```{.sh .copy .separator-dollar}
tar zxvf helm-v2.9.0-rc3-linux-amd64.tar.gz
```

Move into an appropriate location.

```{.sh .copy .separator-dollar}
sudo mv linux-amd64/helm /usr/local/bin/helm
```



