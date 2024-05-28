## yba provider gcp create

Create a GCP YugabyteDB Anywhere provider

### Synopsis

Create a GCP provider in YugabyteDB Anywhere

```
yba provider gcp create [flags]
```

### Examples

```
yba provider gcp create -n dkumar-cli \
	--network yugabyte-network \
	--region region-name=us-west1,shared-subnet=<subnet> \
	--region region-name=us-west2,shared-subnet=<subnet> \
	--credentials <path-to-credentials-file>
```

### Options

```
      --credentials string                    GCP Service Account credentials file path. Required if use-host-credentials is set to false.. Can also be set using environment variable GOOGLE_APPLICATION_CREDENTIALS.
      --use-host-credentials                  [Optional] Enabling YugabyteDB Anywhere Host credentials in GCP. (default false)
      --network string                        [Optional] Custom GCE network name. Required if create-vpc is true or use-host-vpc is false.
      --yb-firewall-tags string               [Optional] Tags for firewall rules in GCP.
      --create-vpc                            [Optional] Creating a new VPC network in GCP (Beta Feature). Specify VPC name using --network. (default false)
      --use-host-vpc                          [Optional] Using VPC from YugabyteDB Anywhere Host. If set to false, specify an exsiting VPC using --network. Ignored if create-vpc is set. (default false)
      --project-id string                     [Optional] Project ID that hosts universe nodes in GCP.
      --shared-vpc-project-id string          [Optional] Shared VPC project ID in GCP.
      --region stringArray                    [Required] Region associated with the GCP provider. Minimum number of required regions = 1. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,shared-subnet=<subnet-id>,yb-image=<custom-ami>,instance-template=<instance-templates-for-YugabyteDB-nodes>". Region name and Shared subnet are required key-value pairs. YB Image (AMI) and Instance Template are optional. Each region can be added using separate --region flags. Example: --region region-name=us-west1,shared-subnet=<shared-subnet-id>
      --ssh-user string                       [Optional] SSH User to access the YugabyteDB nodes. (default "centos")
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

* [yba provider gcp](yba_provider_gcp.md)	 - Manage a YugabyteDB Anywhere GCP provider

