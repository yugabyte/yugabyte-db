YugaWare ensures that YugabyteDB nodes run inside your own AWS account and are secured by a dedicated VPC and Key Pair. To that end, YugaWare will require access to your cloud infrastructure, which it can do in one of two ways:

- directly provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html)
- attach an [IAM role](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html) to the YugaWare VM in the EC2 tab

Once you decide which access method is right for you, it is time to consider deployment options. YugaWare currently supports 3 modes of deploying YugabyteDB nodes:

- **Use the same VPC as the YugaWare machine.** This will setup a custom Security Group to be attached to the Yugabyte nodes, so communication is guaranteed to work. It will also setup a new Key Pair, to be used when spinning up EC2 instances.

- **Specify a different VPC and the region in which it lives.** This will also setup a custom Security Group in the given VPC, as well as a Key Pair. Note however, that it is your responsibility to have already setup Routing Table entries in both VPCs, to ensure network traffic is properly routed!

- **Let YugaWare configure, own and manage a full cross-region deployment of custom VPCs.** This will generate a custom VPC in each available region, then interconnect them, as well as the YugaWare VPC, through VPC-peering. This will also setup all the other relevant sub-components in all regions, such as Key Pairs, Subnets, Security Groups and Routing Table entries.

Note that the AWS Account Name should be unique for each instance of YugaWare integrating with a given AWS Account.

Now we are ready to create a YugabyteDB universe on AWS.
