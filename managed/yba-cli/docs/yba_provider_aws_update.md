## yba provider aws update

Update an AWS YugabyteDB Anywhere provider

### Synopsis

Update an AWS provider in YugabyteDB Anywhere

```
yba provider aws update [flags]
```

### Options

```
      --new-name string             [Optional] Updating provider name.
      --access-key-id string        [Optional] AWS Access Key ID. Required if provider does not use IAM instance profile. Required with secret-access-key. 
      --secret-access-key string    [Optional] AWS Secret Access Key. Required if provider does not use IAM instance profile. Required with access-key-id.
      --use-iam-instance-profile    [Optional] Use IAM Role from the YugabyteDB Anywhere Host.
      --hosted-zone-id string       [Optional] Updating Hosted Zone ID corresponding to Amazon Route53.
      --add-region stringArray      [Optional] Add region associated with the AWS provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,vpc-id=<vpc-id>,sg-id=<security-group-id>,arch=<architecture>". Region name is required key-value. VPC ID, Security Group ID and Architecture are optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray        [Optional] Zone associated to the AWS Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,secondary-subnet=<secondary-subnet-id>". Zone name, Region name and subnet IDs are required values. Secondary subnet ID is optional. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided for new and existing regions.Each zone needs to be added using a separate --add-zone flag.
      --remove-region stringArray   [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray     [Optional] Remove zone associated to the AWS Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --edit-region stringArray     [Optional] Edit region details associated with the AWS provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,vpc-id=<vpc-id>,sg-id=<security-group-id>". Region name is a required key-value pair. VPC ID and Security Group ID are optional. Each region needs to be modified using a separate --edit-region flag.
      --edit-zone stringArray       [Optional] Edit zone associated to the AWS Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>,secondary-subnet=<secondary-subnet-id>". Zone name, Region name are required values. Subnet IDs and Secondary subnet ID is optional. Each zone needs to be modified using a separate --edit-zone flag.
      --ssh-user string             [Optional] Updating SSH User to access the YugabyteDB nodes.
      --ssh-port int                [Optional] Updating SSH Port to access the YugabyteDB nodes.
      --airgap-install              [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray     [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                        help for update
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

