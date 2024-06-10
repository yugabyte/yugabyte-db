---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  preview_yugabyte-platform:
    identifier: cloud-permissions-nodes-2-aws
    parent: cloud-permissions
    weight: 20
type: docs
---

For YugabyteDB Anywhere (YBA) to be able to deploy and manage YugabyteDB clusters, you need to provide YBA with privileges on your cloud infrastructure to create, delete, and modify VMs, mount and unmount disk volumes, and so on.

The more permissions that you can provide, the more YBA can automate.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-permissions-nodes/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-aws/" class="nav-link active">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-gcp" class="nav-link">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-azure" class="nav-link">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-k8s" class="nav-link">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

## AWS

The following permissions are required for AWS.

```yaml
{
   "Version": "2012-10-17",
   "Statement": [
       {
           "Sid": "VisualEditor0",
           "Effect": "Allow",
           "Action": [
               "ec2:AttachVolume",
               "ec2:AuthorizeSecurityGroupIngress",
               "ec2:ImportVolume",
               "ec2:ModifyVolumeAttribute",
               "ec2:DescribeInstances",
               "ec2:DescribeInstanceAttribute",
               "ec2:CreateKeyPair",
               "ec2:DescribeVolumesModifications",
               "ec2:DeleteVolume",
               "ec2:DescribeVolumeStatus",
               "ec2:StartInstances",
               "ec2:DescribeAvailabilityZones",
               "ec2:DescribeVolumes",
               "ec2:ModifyInstanceAttribute",
               "ec2:DescribeKeyPairs",
               "ec2:DescribeInstanceStatus",
               "ec2:DetachVolume",
               "ec2:ModifyVolume",
               "ec2:TerminateInstances",
               "ec2:AssignIpv6Addresses",
               "ec2:ImportKeyPair",
               "ec2:DescribeTags",
               "ec2:CreateTags",
               "ec2:RunInstances",
               "ec2:AssignPrivateIpAddresses",
               "ec2:StopInstances",
               "ec2:AllocateAddress",
               "ec2:DescribeVolumeAttribute",
               "ec2:DescribeSecurityGroups",
               "ec2:CreateVolume",
               "ec2:EnableVolumeIO",
               "ec2:DescribeImages",
               "ec2:DescribeVpcs",
               "ec2:DeleteSecurityGroup",
               "ec2:DescribeSubnets",
               "ec2:DeleteKeyPair",
               "ec2:DescribeVpcPeeringConnections",
               "ec2:DescribeRouteTables",
               "ec2:DescribeInternetGateways",
               "ec2:GetConsoleOutput",
               "ec2:CreateSnapshot",
               "ec2:DeleteSnapshot",
               "ec2:DescribeInstanceTypes"
           ],
           "Resource": "*"
       }
   ]
}
```

To grant the required access, you do one of the following:

- Create a service account with the permissions. You'll later provide YBA with the service account Access key ID and Secret Access Key when creating the provider.
- Attach an IAM role with the required permissions to the EC2 VM instance where YugabyteDB Anywhere will be running.

### Service account

If using a service account, record the following two pieces of information about your service account. You will need to provide this information later to YBA.

| Save for later | To configure |
| :--- | :--- |
| Access key ID | [AWS cloud provider](../../../configure-yugabyte-platform/aws/) |
| Secret Access Key | [AWS cloud provider](../../../configure-yugabyte-platform/aws/) |

### IAM role

If attaching an IAM role to the YBA EC2 VM, you must also execute the following command to change metadata options:

```sh
aws ec2 modify-instance-metadata-options --instance-id i-NNNNNNN --http-put-response-hop-limit 3 --http-endpoint enabled --region us-west-2
```

Replace NNNNNNN with the instance ID and us-west-2 with the region in which this EC2 VM is deployed.

For more information, see [Configure the instance metadata service](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configuring-instance-metadata-service.html) in the AWS documentation.

### Provide access to AWS AMIs

In addition to AWS cloud permissions, to create VMs on AWS YBA needs access the operating system disk images, known as AMIs.

You must grant this access, and also accept any OS licensing terms manually before providing this access to YBA.

By default, YBA requires access to the Alma 8 disk x86 disk images.

#### Default case

If you plan to use YBA defaults, then, while logged into your AWS account, do the following:

- For the default x86 AMI, go to [AlmaLinux OS 8 (x86_64) product](https://aws.amazon.com/marketplace/pp/prodview-mku4y3g4sjrye) and click **Continue to Subscribe**.
- For the default aarch64 AMI, go to [AlmaLinux OS 8 (arm64) product](https://aws.amazon.com/marketplace/pp/prodview-zgsymdwitnxmm) and click **Continue to Subscribe**.

If you are not already subscribed and have not accepted the **Terms and Conditions**, then you should see the following message:

![Marketplace accept](/images/ee/aws-setup/marketplace-accept.png)

Click **Accept Terms**.

If needed, be sure to do this in every region where you intend to deploy database clusters.

#### Custom disk image

If you plan to use a custom operating system and disk image, then verify that the [service account](#service-account) or [IAM role](#iam-role) that you provisioned earlier has access to the required OS disk image (that is, the specific AMI).

If needed, be sure to do this in every region where you intend to deploy database clusters.

## Managing SSH keys for VMs

When creating VMs on the public cloud, YugabyteDB requires SSH keys to access the VM. You can manage the SSH keys for VMs in two ways:

- YBA managed keys. When YBA creates VMs, it will generate and manage the SSH key pair.
- Provide a custom key pair. Create your own custom SSH keys and upload the SSH keys when you create the provider.

If you will be using your own custom SSH keys, then ensure that you have them when installing YBA and creating your public cloud provider.

| Save for later | To configure |
| :--- | :--- |
| Custom SSH keys | [AWS provider](../../../configure-yugabyte-platform/kubernetes/) |
