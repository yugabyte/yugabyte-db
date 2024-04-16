## yba provider aws create

Create an AWS YugabyteDB Anywhere provider

### Synopsis

Create an AWS provider in YugabyteDB Anywhere

```
yba provider aws create [flags]
```

### Examples

```
yba provider aws create -n <provider-name> \
	--region region-name=us-west-2,vpc-id=<vpc-id>,sg-id=<security-group> \
	--zone zone-name=us-west-2a,region-name=us-west-2,subnet=<subnet> \
	--zone zone-name=us-west-2b,region-name=us-west-2,subnet=<subnet> \
	--access-key-id <aws-access-key-id> --secret-access-key <aws-secret-access-key>
```

### Options

```
      --access-key-id string                  AWS Access Key ID. Required for non IAM role based providers. Can also be set using environment variable AWS_ACCESS_KEY_ID.
      --secret-access-key string              AWS Secret Access Key. Required for non IAM role based providers. Can also be set using environment variable AWS_SECRET_ACCESS_KEY.
      --use-iam-instance-profile              [Optional] Use IAM Role from the YugabyteDB Anywhere Host. Provider creation will fail on insufficient permissions on the host. (default false)
      --hosted-zone-id string                 [Optional] Hosted Zone ID corresponding to Amazon Route53.
      --region stringArray                    [Required] Region associated with the AWS provider. Minimum number of required regions = 1. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,vpc-id=<vpc-id>,sg-id=<security-group-id>,arch=<architecture>,yb-image=<custom-ami>". Region name is required key-value. VPC ID, Security Group ID, YB Image (AMI) and Architecture (Default to x86_84) are optional. Each region needs to be added using a separate --region flag. Example: --region region-name=us-west-2,vpc-id=<vpc-id>,sg-id=<security-group> --region region-name=us-east-2,vpc-id=<vpc-id>,sg-id=<security-group>
      --zone stringArray                      [Required] Zone associated to the AWS Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,secondary-subnet=<secondary-subnet-id>". Zone name, Region name and subnet IDs are required values. Secondary subnet ID is optional. Each --region definition must have atleast one corresponding --zone definition. Multiple --zone definitions can be provided per region.Each zone needs to be added using a separate --zone flag. Example: --zone zone-name=us-west-2a,region-name=us-west-2,subnet=<subnet-id> --zone zone-name=us-west-2b,region-name=us-west-2,subnet=<subnet-id>
      --ssh-user string                       [Optional] SSH User to access the YugabyteDB nodes. (default "ec2-user")
      --ssh-port int                          [Optional] SSH Port to access the YugabyteDB nodes. (default 22)
      --custom-ssh-keypair-name string        [Optional] Provide custom key pair name to access YugabyteDB nodes. If left empty, YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.
      --custom-ssh-keypair-file-path string   [Optional] Provide custom key pair file path to access YugabyteDB nodes. Required with --custom-ssh-keypair-name.
      --airgap-install                        [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray               [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                                  help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider aws](yba_provider_aws.md)	 - Manage a YugabyteDB Anywhere AWS provider

