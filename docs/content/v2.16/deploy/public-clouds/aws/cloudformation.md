---
title: Deploy on Amazon Web Services using AWS CloudFormation
headerTitle: Amazon Web Services
linkTitle: Amazon Web Services
description: Deploy a YugabyteDB cluster on Amazon Web Services using AWS CloudFormation
menu:
  v2.16:
    identifier: deploy-in-aws-1-cloudformation
    parent: public-clouds
    weight: 630
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../cloudformation/" class="nav-link active">
      <i class="icon-shell"></i>
      CloudFormation
    </a>
  </li>
  <li >
    <a href="../terraform/" class="nav-link">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>
  <li>
    <a href="../manual-deployment/" class="nav-link">
      <i class="icon-shell"></i>
      Manual deployment
    </a>
  </li>
</ul>

## Prerequisites

1. You need to have an IAM user who has `AWSCloudFormationFullAccess`, `AmazonEC2FullAccess` and `ssm:GetParameters` privilege.
2. If you are going to create the stack via AWS Console then make sure the user also has `s3:CreateBucket`, `s3:PutObject` and `s3:GetObject` policies.
3. Create an SSH key pair that you want to attach to the nodes.
4. In the region you want to bring up the stack, make sure you can launch new VPCs.
5. Download the template file.

```sh
$ wget https://raw.githubusercontent.com/yugabyte/aws-cloudformation/master/yugabyte_cloudformation.yaml
```

{{< note title="Note" >}}

When using an instance with local disks (as opposed to EBS), the `.yaml` file needs to be changed for YugabyteDB to recognize the local disks, as per [this example](https://github.com/yugabyte/aws-cloudformation/blob/master/yugabyte_ephemeral_nvme_cloudformation.yaml) that demonstrates the use of different instance types.

{{< /note >}}

## AWS Command Line

Create CloudFormation template:

```sh
$ aws cloudformation create-stack \
  --stack-name <stack-name> \
  --template-body file://yugabyte_cloudformation.yaml \
  --parameters  ParameterKey=DBVersion,ParameterValue=2.1.6.0-b17 ParameterKey=KeyName,ParameterValue=<ssh-key-name> \
  --region <aws-region>
```

Once resource creation completes, check that stack was created:

```sh
$ aws cloudformation describe-stacks \
  --stack-name <stack-name> \
  --region <aws-region>
```

From this output, you can get the VPC ID and YugabyteDB Admin URL.

Because the stack creates a security group that restricts access to the database, you might need to update the security group inbound rules if you have trouble connecting to the DB.

## AWS Console

1. Navigate to your AWS console and open the CloudFormation dashboard. Click **Create Stack**.

    ![Cloud Formation dashboard](/images/deploy/aws/aws-cf-initial-dashboard.png)

2. Prepare a template using the downloaded template.

    ![Prepare template](/images/deploy/aws/aws-cf-prepare-template.png)

3. Specify the template file downloaded in Prerequisites: `yugabyte_cloudformation.yaml`

    ![Upload template](/images/deploy/aws/aws-cf-upload-template.png)

4. Provide the required parameters. Each of these fields is prefilled with information from the configuration YAML file that was uploaded. `LatestAmiId` refers to the ID of the machine image to use, see [Find a Linux AMI](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/finding-an-ami.html).

    ![Provide Parameters](/images/deploy/aws/aws-cf-provide-parameters.png)

5. Configure stack options. For more information, see [Setting AWS CloudFormation stack options](https://docs.aws.amazon.com/AWSCloudFormation/latest/UserGuide/cfn-console-add-tags.html).

    ![Configure options](/images/deploy/aws/aws-cf-configure-options.png)

6. Finalize changes before creation.

    ![Review stack](/images/deploy/aws/aws-cf-review-stack.png)

7. Check stack output in the dashboard.

    ![Check output](/images/deploy/aws/aws-cf-check-output.png)
