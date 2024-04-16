## yba provider gcp update

Update a GCP YugabyteDB Anywhere provider

### Synopsis

Update a GCP provider in YugabyteDB Anywhere

```
yba provider gcp update [flags]
```

### Options

```
      --new-name string                [Optional] Updating provider name.
      --use-host-credentials           [Optional] Enabling YugabyteDB Anywhere Host credentials in GCP. Explicitly mark as false to disable on a provider made with host credentials.
      --credentials string             [Optional] GCP Service Account credentials file path. Required for providers not using host credentials.
      --network string                 [Optional] Update Custom GCE network name. Required if create-vpc is true or use-host-vpc is false.
      --yb-firewall-tags string        [Optional] Update tags for firewall rules in GCP.
      --create-vpc                     [Optional] Creating a new VPC network in GCP (Beta Feature). Specify VPC name using --network.
      --use-host-vpc                   [Optional] Using VPC from YugabyteDB Anywhere Host. If set to false, specify an exsiting VPC using --network. Ignored if create-vpc is set.
      --project-id string              [Optional] Update project ID that hosts universe nodes in GCP.
      --shared-vpc-project-id string   [Optional] Update shared VPC project ID in GCP.
      --add-region stringArray         [Required] Region associated with the GCP provider. Minimum number of required regions = 1. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,instance-template=<instance-templates-for-YugabyteDB-nodes>". Region name and Shared subnet are required key-value pairs. YB Image (AMI) and Instance Template are optional. Each region can be added using separate --add-region flags.
      --edit-region stringArray        [Optional] Edit region details associated with the GCP provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,instance-template=<instance-templates-for-YugabyteDB-nodes>". Region name is a required key-value pair. Shared subnet, YB Image (AMI) and Instance Template are optional. Each region needs to be modified using a separate --edit-region flag.
      --remove-region stringArray      [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --ssh-user string                [Optional] Updating SSH User to access the YugabyteDB nodes.
      --ssh-port int                   [Optional] Updating SSH Port to access the YugabyteDB nodes.
      --airgap-install                 [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads.
      --ntp-servers stringArray        [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                           help for update
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

* [yba provider gcp](yba_provider_gcp.md)	 - Manage a YugabyteDB Anywhere GCP provider

