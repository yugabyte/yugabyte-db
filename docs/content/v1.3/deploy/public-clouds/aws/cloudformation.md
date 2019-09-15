## Prerequisites

Download the template file 
```sh
$ wget https://raw.githubusercontent.com/yugabyte/aws-cloudformation/master/yugabyte_cloudformation.yaml
```

## AWS Command Line

Create CloudFormation template:

```sh
$ aws cloudformation create-stack --stack-name <stack-name> --template-body yugabyte_cloudformation.yaml --parameters DBVersion=1.2.8.0, KeyName=<ssh-key-name>
```
Once resource creation completes, check that stack was created:

```sh
$ aws cloudformation describe-stacks --stack-name <stack-name>
```
From this output, you will be able to get the VPC id and YugaByte DB admin URL.

## AWS Console

1. Navigate to your AWS console and open the CloudFormation dashboard. Click `Create Stack`
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
5. Configure Stack options. For more information about these options click [here](https://docs.aws.amazon.com/AWSCloudFormation/latest/UserGuide/cfn-console-add-tags.html).
<img title="Configure options" class="expandable-image" src="/images/deploy/aws/aws-cf-configure-options.png" />
<br>
6. Finalize changes before creation
<img title="Review stack" class="expandable-image" src="/images/deploy/aws/aws-cf-review-stack.png" />
<br>
7. Check stack output in dashboard
<img title="Check output" class="expandable-image" src="/images/deploy/aws/aws-cf-check-output.png" />
