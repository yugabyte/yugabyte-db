<!--
+++
private = true
+++
-->

### Network Access

#### Trusted IP Addresses

YugabyteDB Managed only allows access to clusters from trusted IP addresses. For applications in peered VPCs to be able to connect, you need to add the CIDR of the peered VPC to the cluster [IP allow list](../../../cloud-secure-clusters/add-connections/). You can also assign IP allow lists to your cluster any time after the cluster is created.

![Add Cluster Wizard - Network Access Trusted IP Addresses](/images/yb-cloud/cloud-addcluster-networkaccess.png)

You can add IP addresses using any combination of the following options.

| Option | Description |
| :----- | :---------- |
| Add Current IP Address | Creates an allow list using the public IP address of your computer and adds it to the cluster IP allow list. |
| Add Peered VPC Networks | Only available for clusters being deployed in a VPC. VPCs must be peered, and the peering connection active for the peered networks to be added to the IP allow list.<br>Choose **Add All Peered Networks** to create an IP allow list from every network peered with the cluster VPC, and add it to the cluster.<br>Choose **Add Individual Peered Networks** to select specific peered networks to add to the cluster IP allow list. |
| Add Existing IP Allow List | Choose from a list of IP allow lists already created for your account. |
| Create New IP Allow List | Create a new IP allow list and manually enter the CIDR and public IP addresses. |

**Enable Public Access for this Cluster** - To connect to a cluster deployed in a VPC from a public IP address (including your current address), you must enable Public Access for the cluster. When enabled, a public IP address is added to each region of the cluster. You can view the private and public host addresses under **Connection Parameters** on the cluster **Settings > Infrastructure** tab.

#### Private Service Endpoint

For clusters in AWS and Azure, you can connect your cluster to your application VPC using a private link. To do this, you add a private service endpoint (PSE) to each region in your cluster.

You can also [add PSEs](../../cloud-vpcs/cloud-add-endpoint/) after your cluster is created.

![Add Cluster Wizard - Network Access PSE](/images/yb-cloud/cloud-addcluster-networkaccess-pse.png)

To use a private link for network isolation and security, choose **Create Private Service Endpoint**, then add security principals. A security principal is a cloud provider account with permissions to manage endpoints. In AWS, this would be Amazon resource names, and in Azure, Subscription IDs.

After the cluster is created, you still need to complete the private link setup by adding an endpoint on your application VPC for each region in your cluster, and linking them to the PSEs on your cluster.

For more information, refer to [Private service endpoints](../../cloud-vpcs/cloud-add-endpoint/).
