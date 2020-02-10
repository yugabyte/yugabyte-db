## Prerequisites

1. You need to have an IAM user who has `AWSCloudFormationFullAccess` privilege.
2. Create an SSH key pair that you want to attach to the nodes.
3. In the region you want to bring up the stack, make sure you can launch new VPCs.  
4. Download the template file.

```sh
$ wget https://raw.githubusercontent.com/yugabyte/aws-cloudformation/master/yugabyte_cloudformation.yaml
```

{{< note title="Note" >}}

When using an instance with local disks (not EBS), the `.yaml` file needs to be changed for YugabyteDB to recognize the local disks.
Here is an example using [i3 instance types](https://github.com/yugabyte/aws-cloudformation/blob/master/yugabyte_cloudformation_i3_example.yaml) 
that formats and mounts the nvme ssd automatically for each host and installs YugabyteDB on that mount.

{{< /note >}}

## AWS Command Line

Create CloudFormation template:

```sh
$ aws --region <aws-region> cloudformation create-stack --stack-name <stack-name> --template-body file://yugabyte_cloudformation.yaml --parameters  ParameterKey=DBVersion,ParameterValue=2.0.6.0,ParameterKey=KeyName,ParameterValue=<ssh-key-name>
```

Once resource creation completes, check that stack was created:

```sh
$ aws --region <aws-region> cloudformation describe-stacks --stack-name <stack-name>
```

From this output, you will be able to get the VPC id and YugabyteDB admin URL.

Because the stack creates a security group that restricts access to the database, you might need to update the security group inbound rules if you have trouble connecting to it. 
if you have trouble connecting to the DB.

## AWS Console

1. Navigate to your AWS console and open the CloudFormation dashboard. Click **Create Stack**.

<img title="Cloud Formation dashboard" class="expandable-image" src="/images/deploy/aws/aws-cf-initial-dashboard.png" />
<br>

2. Prepare template using the downloaded template

<img title="Prepare template" class="expandable-image" src="/images/deploy/aws/aws-cf-prepare-template.png" />
<br>

3. Specify the template file downloaded in Prerequisites: `yugabyte_cloudformation.yaml`

<img title="Upload template" class="expandable-image" src="/images/deploy/aws/aws-cf-upload-template.png" />
<br>

4. Provide the required parameters. Each of these fields are prefilled with information from the configuration yaml file that was uploaded. `LatestAmiId` refers to the id of the machine image to use, see [more](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/finding-an-ami.html).

<img title="Provide Parameters" class="expandable-image" src="/images/deploy/aws/aws-cf-provide-parameters.png" />
<br>

5. Configure Stack options. For more information about these options, click [here](https://docs.aws.amazon.com/AWSCloudFormation/latest/UserGuide/cfn-console-add-tags.html).

<img title="Configure options" class="expandable-image" src="/images/deploy/aws/aws-cf-configure-options.png" />
<br>

6. Finalize changes before creation

<img title="Review stack" class="expandable-image" src="/images/deploy/aws/aws-cf-review-stack.png" />
<br>

7. Check stack output in dashboard

<img title="Check output" class="expandable-image" src="/images/deploy/aws/aws-cf-check-output.png" />
