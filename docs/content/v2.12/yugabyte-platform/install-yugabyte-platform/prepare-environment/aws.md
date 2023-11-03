---
title: Prepare the Amazon Web Services (AWS) cloud environment
headerTitle: Prepare the Amazon Web Services (AWS) cloud environment
linkTitle: Prepare the environment
description: Prepare the Amazon Web Services (AWS) environment for the Yugabyte Platform.
menu:
  v2.12_yugabyte-platform:
    identifier: prepare-environment-1-aws
    parent: install-yugabyte-platform
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link active">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp" class="nav-link">
       <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

<li>
    <a href="../openshift" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="../on-premises" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

## Create a new security group (optional)

To access Yugabyte Platform from outside the AWS environment, you would need to enable access by assigning an appropriate security group to the Yugabyte Platform machine. You will at minimum need to:

- Access the Yugabyte Platform instance over SSH (port `tcp:22`)
- Check, manage, and upgrade Yugabyte Platform (port `tcp:8800`)
- View the Yugabyte Platform console (port `tcp:80`)

If you are using your own custom VPCs (self-managed configuration), the following additional TCP ports must be accessible: 7000, 7100, 9000, 9100, 11000, 12000, 9300, 9042, 5433, and 6379. For more information on ports used by YugabyteDB, refer to [Default ports](../../../../reference/configuration/default-ports).

To create a security group that enables these, go to **EC2 > Security Groups**, click **Create Security Group** and then add the following values:

- For the name, enter `yugaware-sg` (you can change the name if you want).
- Add a description (for example, `Security group for Yugabyte Platform access`).
- Add the appropriate IP addresses to the **Source IP ranges** field. To allow access from any machine, add `0.0.0.0/0` but note that this is not very secure.
- Add the ports `22`, `8800`, and `80` to the **Port Range** field. The **Protocol** selected must be `TCP`. For a self-managed configuration, also add the 7000, 7100, 9000, 9100, 11000, 12000, 9300, 9042, 5433, and 6379 TCP ports.

You should see something like the screenshot below. Click **Create** next.

![Create security group](/images/ee/aws-setup/yugaware-aws-create-sg.png)

## Create an IAM role (optional)

In order for Yugabyte Platform to manage YugabyteDB nodes, limited access to your AWS infrastructure is required. To grant the required access, you can provide a set of credentials when configuring the AWS provider, as described in [Configure the AWS cloud provider](../../../configure-yugabyte-platform/set-up-cloud-provider/aws). Alternatively, the EC2 instance where the Yugabyte Platform will be running can be brought up with an IAM role with enough permissions to take all the actions required by Yugabyte Platform. Here is a sample of such a role:

```sh
{
    "Version": "2020-10-17",
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
                "ec2:CreateSecurityGroup",
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
                "ec2:DescribeInternetGateways"
            ],
            "Resource": "*"
        }
    ]
}
```

## Provision an instance for Yugabyte Platform

Create an instance to run the Yugabyte Platform server. To do this, navigate to **EC2 > Instances**, click **Launch Instance**, and enter the following values:

- Change the boot disk image to Ubuntu Server 16.04, as shown in the following illustration:

    ![Image](/images/ee/aws-setup/yugaware-create-instance-os.png)

- Select c5.xlarge as the instance type (4 vCPUs are recommended for production).

- Define the VPC, subnet, and other settings as required.

  Ensure that **Auto-assign Public IP** is enabled (if it is disabled, the instance would not be accessible from outside AWS).

  If you created an IAM role, as described in [Create an IAM role](#create-an-iam-role-(optional)), or already had the IAM role that you would like to use, include this information under **IAM role**. See [Deploy the YugabyteDB universe using an IAM role](#deploy-the-yugabytedb-universe-using-an-iam-role) for more information.

- Increase the root storage volume size to at least 100GiB.

- Add a tag to name the instance. You can set key to `Name` and value to `yugaware-1`.

- Select the `yugaware-sg` security group created in the previous step (or the custom name you chose when setting up the security groups), and then launch the instance.

- Pick an existing key pair or create a new one in order to access the instance.

  Ensure that you have the SSH access key because it is required for enabling `ssh` access to the computer. In this example, assume that the key pair is `~/.ssh/yugaware.pem`.

- Click **Launch** to launch the Yugabyte Platform server.

  You should see an instance being created, as shown in the following illustration:

  ![Image](/images/ee/aws-setup/yugaware-machine-creation.png)

### Deploy the YugabyteDB universe using an IAM role

If you are planning to use an IAM role while deploying the universe in your AWS account, you need to attach the IAM role to the Yugabyte Platform VM before launching the instance, as follows:

1. Navigate to **EC2 > New Instance > Confirm Instance Details > Attach the IAM role**.
2. Set the **IAM role** field to your IAM role (for example, ec2-admin-access).
3. Set the **Metadata accessible** field to Enabled.
4. Set the **Metadata version** field to V1 and V2 (token optional).
5. Set the **Metadata token response hop limit** field to 3, as per the following illustration:

  ![AIM for AWS](/images/ee/aws-setup/iam-for-aws.png)

If you are configuring an existing instance of Yugabyte Platform, start by attaching the IAM role as follows:

1. Navigate to **EC2 > Instances**.
2. Select the instance.
3. Navigate to **Actions > Security > Modify IAM role**.
4. Add the IAM role.
5. Click **Save**.

Then execute the following command to change metadata options (replace `NNNNNNN` with the instance ID and `us-west-2` with the region in which this EC2 VM is deployed):

```shell
aws ec2 modify-instance-metadata-options --instance-id i-NNNNNNN --http-put-response-hop-limit 3 --http-endpoint enabled --region us-west-2
```

For more information, see [Configure the instance metadata service](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configuring-instance-metadata-service.html).

When you create an AWS cloud provider, as described in [Configure the AWS cloud provider](../../../configure-yugabyte-platform/set-up-cloud-provider/aws), you need to  complete the following fields in the **Cloud Provider Configuration > AWS** screen:

- Set the **Credential Type** field to Use IAM Role on instance.
- Set the **VPC Setup** field to Create a new VPC.
- Add **US West 1** as a region.
- Click **Save**.
