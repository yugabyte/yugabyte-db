---
title: Deploy on Azure Kubernetes Service (AKS) using Helm Chart
headerTitle: Azure Kubernetes Service (AKS)
linkTitle: Azure Kubernetes Service (AKS)
description: Use Helm Chart to deploy a single-zone Kubernetes cluster on Azure Kubernetes Service (AKS).
menu:
  latest:
    parent: deploy-kubernetes-sz
    name: Azure Kubernetes Service
    identifier: k8s-aks-1
    weight: 624
aliases:
  - /latest/deploy/kubernetes/aks/
  - /latest/deploy/kubernetes/aks/helm-chart/
  - /latest/deploy/kubernetes/single-zone/aks/
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/deploy/kubernetes/single-zone/aks/helm-chart" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Helm chart
    </a>
  </li>
  <li >
    <a href="/latest/deploy/kubernetes/single-zone/aks/statefulset-yaml" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      StatefulSet YAML
    </a>
  </li>
</ul>

<!----- Conversion time: 9.528 seconds.


Using this Markdown file:

1. Cut and paste this output into your source file.
2. See the notes and action items below regarding this conversion run.
3. Check the rendered output (headings, lists, code blocks, tables) for proper
   formatting and use a linkchecker before you publish this page.

Conversion notes:

* Docs to Markdown version 1.0β22
* Fri May 01 2020 16:25:10 GMT-0700 (PDT)
* Source doc: Getting Started with Distributed SQL on a Azure Kubernetes Service Cluster
* This document has images: check for >>>>>  gd2md-html alert:  inline image link in generated source and store images to your server.
----->


<p style="color: red; font-weight: bold">>>>>>  gd2md-html alert:  ERRORs: 0; WARNINGs: 0; ALERTS: 8.</p>
<ul style="color: red; font-weight: bold"><li>See top comment block for details on ERRORs and WARNINGs. <li>In the converted Markdown or HTML, search for inline alerts that start with >>>>>  gd2md-html alert:  for specific instances that need correction.</ul>

<p style="color: red; font-weight: bold">Links to alert messages:</p><a href="#gdcalert1">alert1</a>
<a href="#gdcalert2">alert2</a>
<a href="#gdcalert3">alert3</a>
<a href="#gdcalert4">alert4</a>
<a href="#gdcalert5">alert5</a>
<a href="#gdcalert6">alert6</a>
<a href="#gdcalert7">alert7</a>
<a href="#gdcalert8">alert8</a>

<p style="color: red; font-weight: bold">>>>>> PLEASE check and correct alert issues and delete this message and the inline alerts.<hr></p>


**Getting Started with Distributed SQL on Azure Kubernetes Service **

Microsoft’s [Azure Kubernetes Service](https://azure.microsoft.com/en-au/services/kubernetes-service/) (AKS) offers a highly available, secure, and fully managed Kubernetes service for developers looking to host their applications on containers in the cloud. AKS features elastic provisioning, an integrated developer experience for rapid application development, enterprise security features, and the most available regions of any cloud provider.

**GRAPHIC: YugabyteDB + Microsoft Azure Kubernetes Service **

YugabyteDB is a natural fit for AKS as It was designed to support cloud-native environments since its initial design.

**_What’s YugabyteDB? It is an open source, high-performance distributed SQL database built on a scalable and fault-tolerant design inspired by Google Spanner. Yugabyte’s SQL API (YSQL) is PostgreSQL wire compatible._**

If your organization is looking to build an application that requires Kubernetes support, geo distributed data, high performance and availability, plus the familiarity of SQL, then YugabyteDB should be on your shortlist of databases to use as a backend.

In this blog, we’ll show you just how easy it is to get started with YugabyteDB on AKS in just a few minutes. As a bonus, we’ll also show you how to deploy the familiar Northwind sample database.

** \
Pre-requisites  \
**

Below is a list of the various components we’ll be installing and configuring for the purposes of this post:



*   YugabyteDB [version 2.1.4](https://docs.yugabyte.com/latest/deploy/kubernetes/single-zone/oss/helm-chart/)
*   Kubectl [version 1.18.0](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.18/)
*   Helm [version 3.1.0](https://helm.sh/docs/)
*   A [Microsoft Azure](https://azure.microsoft.com/en-au/pricing/purchase-options/pay-as-you-go/) account with “Pay As You Go” enabled

To get YugabyteDB up and running an an Azure Kubernetes cluster we’ll need to perform the following steps:



*   Step 1: Install the Azure CLI
*   Step 2: Create a resource group
*   Step 3: Create a Kubernetes cluster
*   Step 4: Install YugabyteDB
*   Bonus: Install the Northwind sample database

**Step 1: Install the Azure CLI**

Before we create our Kubernetes cluster, let’s first install the Azure CLI on our local machine. To install the Azure CLI on your operating system follow the instructions in the link [here](https://docs.microsoft.com/en-us/cli/azure/install-azure-cli?view=azure-cli-latest).

On Mac, you can install the CLI via Homebrew with:


```
$ brew update && brew install azure-cli
```


Once installed, login at the command line with:


```
$ az login
```


...or open up a browser page at [https://aka.ms/devicelogin](https://aka.ms/devicelogin) and enter the authorization code displayed in your terminal. You should now be able to access the Azure Portal UI by going to [https://portal.azure.com/](https://portal.azure.com/).

**Step 2: Create a Resource Group**

To create a resource group, we’ll need to first choose the location where it will be hosted. Run the following command to retrieve a list of the available locations:


```
$ az account list-locations
```


For the purposes of this demo we are going to choose the “West US” location.


```
 {
    "displayName": "West US",
    "id": "/subscriptions/53f36dd9-85d8-4690-b45b-92733d97e6c3/locations/westus",
    "latitude": "37.783",
    "longitude": "-122.417",
    "name": "westus",
    "subscriptionId": null
  },
```


Next, create the resource group by running the following command using whatever location you decided on:


```
$ az group create --name yugabytedbRG --location westus

{
  "id": "/subscriptions/53f36dd9-85d8-4690-b45b-92733d97e6c3/resourceGroups/yugabytedbRG",
  "location": "westus",
  "managedBy": null,
  "name": "yugabytedbRG",
  "properties": {
    "provisioningState": "Succeeded"
  },
  "tags": null,
  "type": "Microsoft.Resources/resourceGroups"
}
```


You should now be able to view the “yugabytedbRG” resource group in the Azure Portal by clicking on _Resource Groups_.



<p id="gdcalert1" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started0.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert2">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/Getting-Started0.png "image_tooltip")


**Step 3: Create the Kubernetes Cluster**

We can now create our Kubernetes cluster by running the following command. Note that we have not [specified any zones](https://docs.microsoft.com/en-us/azure/aks/availability-zones) in the command below which means the AKS control plane components for the cluster will essentially be deployed in a single zone.


```
$ az aks create \
--resource-group yugabytedbRG \
--name yugabytedbAKSCluster \
--node-count 3 \
--node-vm-size Standard_D2s_v3 \
--enable-addons monitoring \
--generate-ssh-keys
```


The `--generate-ssh-keys` argument auto-generates SSH public and private key files that will be stored in the `~/.ssh` directory.

You should see the following output:


```
Finished service principal creation[###################]  100.0000%
 - Running ..
```


We should now see ”yugabytedbAKSCluster” in the UI.



<p id="gdcalert2" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started1.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert3">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/Getting-Started1.png "image_tooltip")


To create the cluster and use your own ssh keys, run the following command:


```
$ ssh-keygen -t rsa -b 2048
```


Follow the prompts to create the` id_rsa `and `id_rsa.pub` files and note the location where they are stored. Now, execute:


```
$ az aks create \
--resource-group yugabytedbRG \
--name yugabytedbAKSCluster \
--node-count 3 \
--node-vm-size Standard_D2s_v3 \
--enable-addons monitoring \
--ssh-key-value <path_to>id_rsa.pub
```


Once the cluster is installed, point` kubectl` to the cluster by running the following command:


```
$ az aks get-credentials --resource-group yugabytedbRG --name yugabytedbAKSCluster
```


You should see output similar to:


```
Merged "yugabytedbAKSCluster" as current context in /Users/jguerrero/.kube/config
```


If you generated your own ssh-keys, point `kubectl `to the cluster by running the following command instead:


```
$ az aks get-credentials --resource-group yugabytedbRG --name yugabytedbAKSCluster -ssh-key-file <path_to>id_rsa
```


Verify that the cluster nodes are running using the following command:


```
$ kubectl get nodes
```


You should see output similar to:



<p id="gdcalert3" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started2.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert4">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/Getting-Started2.png "image_tooltip")


You can also view the details of the cluster in the Kubernetes Dashboard by running the following commands:


```
$ kubectl create clusterrolebinding kubernetes-dashboard --clusterrole=cluster-admin --serviceaccount=kube-system:kubernetes-dashboard

$ az aks browse --resource-group yugabytedbRG --name yugabytedbAKSCluster
```


Running the command above will open a browser window where you can view the Kubernetes Dashboard: 



<p id="gdcalert4" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started3.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert5">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/Getting-Started3.png "image_tooltip")


**Step 4: Install YugabyteDB using Helm Chart**

Now that we have our Kubernetes cluster up and running, we'll need to perform the following steps to get YugabyteDB deployed: \




*   Add the YugabyteDB Helm repo
*   Create a namespace
*   Install YugabyteDB using Helm

Let’s first add the YugabyteDB repo by running the following commands:


```
$ helm repo add yugabytedb https://charts.yugabyte.com

"yugabytedb" has been added to your repositories

$ helm repo update

Hang tight while we grab the latest from your chart repositories...
...Successfully got an update from the "yugabytedb" chart repository

$ helm search repo yugabytedb/yugabyte

NAME               	CHART VERSION	APP VERSION	DESCRIPTION                                       
yugabytedb/yugabyte	2.1.4        	2.1.4.0-b5 	YugabyteDB is the high-performance distributed ...
```


Create the _yb-demo_ namespace.


```
$ kubectl create namespace yb-demo

namespace/yb-demo created
```


Next we’ll install YugabyteDB in the _yb-demo_ namespace by executing the following commands to specify settings for resource constrained environments.


```
$ helm install yb-demo -n yb-demo yugabytedb/yugabyte \
--set storage.master.count=1 \
--set storage.tserver.count=1 \
--set resource.master.requests.cpu=0.5 \
--set resource.master.requests.memory=1Gi \
--set resource.tserver.requests.cpu=0.5 \
--set resource.tserver.requests.memory=1Gi \
--set resource.master.limits.cpu=0.8 \
--set resource.master.limits.memory=1Gi \
--set resource.tserver.limits.cpu=0.8 \
--set resource.tserver.limits.memory=1Gi \
--timeout=15m
```


Depending on your resources, it may  take some time to get everything installed, deployed and configured. Once the command prompt returns with success, let’s verify that the YugabyteDB pods are running using the following command:


```
$ kubectl get pods --namespace yb-demo
```




<p id="gdcalert5" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started4.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert6">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>


![alt_text](images/Getting-Started4.png "image_tooltip")


To access the Yugabyte Admin UI run the following command to locate the _External IP_ entry associated with _yb-master-ui_ and port 7000.


```
$ kubectl get services --namespace yb-demo
```


Now, go to [http://EXTERNAL_IP:7000](http://ETERNAL_IP:7000). Replace EXTERNAL_IP with your own. You should see the following:



<p id="gdcalert6" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started5.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert7">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>

![alt_text](images/Getting-Started5.png "image_tooltip")

**Bonus: Installing the Northwind Sample Database**

If you’d like to install a sample database and you are coming from the Microsoft or SQL Server world, the [Northwind database](https://docs.yugabyte.com/latest/sample-data/northwind/) is a familiar one to install and get started with.

<p id="gdcalert7" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started6.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert8">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>

![alt_text](images/Getting-Started6.png "image_tooltip")

The following files that will be used to create and populate the example tables are already present on the YugabyteDB pods:

* [Northwind_ddl](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/northwind_ddl.sql): Contains DDL statements to create tables and other database objects.
* [Northwind_data](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/northwind_data.sql): Contains DML statements to load the sample data.

Connect to the YSQL shell by running the following commands:


```
$ kubectl exec -n yb-demo -it yb-tserver-0 /bin/bash

[root@yb-tserver-0 yugabyte]# /home/yugabyte/bin/ysqlsh -h yb-tserver-0.yb-tservers.yb-demo
```


 \
Create the Northwind database and the tables by running the following commands from within the YSQL shell:


```
yugabyte=# CREATE DATABASE northwind;
CREATE DATABASE
yugabyte=# \c northwind
You are now connected to database "northwind" as user "yugabyte".
northwind=# \i share/northwind_ddl.sql
northwind=# \i share/northwind_data.sql
```


We can do a simple verification that the data has been inserted into the tables by running the following command:

```
northwind=# SELECT customer_id, company_name, contact_name FROM customers LIMIT 2;
```

<p id="gdcalert8" ><span style="color: red; font-weight: bold">>>>>>  gd2md-html alert: inline image link here (to images/Getting-Started7.png). Store image on your image server and adjust path/filename if necessary. </span><br>(<a href="#">Back to top</a>)(<a href="#gdcalert9">Next alert</a>)<br><span style="color: red; font-weight: bold">>>>>> </span></p>

![alt_text](images/Getting-Started7.png "image_tooltip")

**Conclusion**

That’s it, you now have YugabyteDB and the Northwind sample database running on an Azure Kubernetes cluster! At this point you are ready to start exploring YSQL’s capabilities. We recommend starting with the [YSQL docs](https://docs.yugabyte.com/latest/api/ysql/) and then reading up on the YugabyteDB’s [architecture](https://docs.yugabyte.com/latest/architecture/), and [design goals.](https://docs.yugabyte.com/latest/architecture/design-goals/)
