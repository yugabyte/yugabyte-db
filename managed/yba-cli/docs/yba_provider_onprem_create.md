## yba provider onprem create

Create an On-premises YugabyteDB Anywhere provider

### Synopsis

Create an On-premises provider in YugabyteDB Anywhere. To utilize the on-premises provider in universes, manage instance types and node instances using the "yba provider onprem instance-type/node [operation]" set of commands

```
yba provider onprem create [flags]
```

### Examples

```
yba provider onprem create --name <provider-name> \
	--region region-name=region1 --region region-name=region2 \
	--zone zone-name=zone1::region-name=region1 \
	--zone zone-name=zone2::region-name=region2 \
	--ssh-user centos \
	--ssh-keypair-name <keypair-name>  \
	--ssh-keypair-file-path <path-to-ssh-key-file>
```

### Options

```
      --ssh-keypair-name string            [Optional] Provider key pair name to access YugabyteDB nodes.
      --ssh-keypair-file-path string       [Optional] Provider key pair file path to access YugabyteDB nodes. One of ssh-keypair-file-path or ssh-keypair-file-contents is required if --ssh-keypair-name is provided.
      --ssh-keypair-file-contents string   [Optional] Provider key pair file contents to access YugabyteDB nodes. One of ssh-keypair-file-path or ssh-keypair-file-contents is required if --ssh-keypair-name is provided.
      --ssh-user string                    [Optional] SSH User to access YugabyteDB nodes. Required when --skip-provisioning is false.
      --ssh-port int                       [Optional] SSH Port. (default 22)
      --region stringArray                 [Required] Region associated with the On-premises provider. Minimum number of required regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::latitude=<latitude>::longitude=<longitude>". Region name is a required key-value. Latitude and Longitude (Default values for both are 0.0) are optional. Each region needs to be added using a separate --region flag. Example: --region region-name=us-west-1 --region region-name=us-west-2
      --zone stringArray                   [Required] Zone associated to the On-premises Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>". Zone name and Region name are required values. Each --region definition must have atleast one corresponding --zone definition. Multiple --zone definitions can be provided per region. Each zone needs to be added using a separate --zone flag. Example: --zone zone-name=us-west-1a::region-name=us-west-1 --zone zone-name=us-west-1b::region-name=us-west-1
      --passwordless-sudo-access           [Optional] Can sudo actions be carried out by user without a password. (default true)
      --skip-provisioning                  [Optional] Set to true if YugabyteDB nodes have been prepared manually, set to false to provision during universe creation. (default false)
      --airgap-install                     [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --install-node-exporter              [Optional] Install Node exporter. (default true)
      --node-exporter-user string          [Optional] Node Exporter User. (default "prometheus")
      --node-exporter-port int             [Optional] Node Exporter Port. (default 9300)
      --ntp-servers stringArray            [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
      --yb-home-dir string                 [Optional] YB Home directory.
      --use-clockbound                     [Optional] Configure and use ClockBound for clock synchronization. Requires ClockBound to be set up on the nodes. (default false)
  -h, --help                               help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update, instance-type and node.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider onprem](yba_provider_onprem.md)	 - Manage a YugabyteDB Anywhere on-premises provider

