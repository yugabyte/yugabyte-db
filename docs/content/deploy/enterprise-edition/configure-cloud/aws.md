YugaWare ensures that YugaByte DB nodes run inside your own AWS account and are secured by a dedicated VPC and Key Pair. After you provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html), YugaWare invokes AWS APIs to perform the following actions. Note that the AWS Account Name should be unique for each instance of YugaWare integrating with a given AWS Account.

1. Retrieves the regions/AZs as well as the available instance types configured for this AWS account and initializes its own Amazon cloud provider.

2. Creates a new [AWS Key Pair](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html) to be used to SSH into the YugaByte instances. The private key will be available for download later from the YugaWare UI.

3. Creates a new [AWS VPC](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-vpc.html) for YugaByte instances and then peers them with YugaWare's own VPC

![Configure AWS](/images/ee/configure-aws-1.png)

![AWS Configuration in Progress](/images/ee/configure-aws-2.png)

![AWS Configured Successfully](/images/ee/configure-aws-3.png)

Now we are ready to create a YugaByte universe on AWS.
