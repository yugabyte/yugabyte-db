## yba provider gcp update

Update a GCP YugabyteDB Anywhere provider

### Synopsis

Update a GCP provider in YugabyteDB Anywhere

```
yba provider gcp update [flags]
```

### Examples

```
yba provider gcp update --name <provider-name> --new-name <new-provider-name>
```

### Options

```
      --new-name string                   [Optional] Updating provider name.
      --use-host-credentials              [Optional] Enabling YugabyteDB Anywhere Host credentials in GCP. Explicitly mark as false to disable on a provider made with host credentials.
      --credentials string                [Optional] GCP Service Account credentials file path. Required for providers not using host credentials.
      --network string                    [Optional] Update Custom GCE network name. Required if create-vpc is true or use-host-vpc is false.
      --yb-firewall-tags string           [Optional] Update tags for firewall rules in GCP.
      --create-vpc                        [Optional] Creating a new VPC network in GCP (Beta Feature). Specify VPC name using --network.
      --use-host-vpc                      [Optional] Using VPC from YugabyteDB Anywhere Host. If set to false, specify an exsiting VPC using --network. Ignored if create-vpc is set.
      --project-id string                 [Optional] Update project ID that hosts universe nodes in GCP.
      --shared-vpc-project-id string      [Optional] Update shared VPC project ID in GCP.
      --add-region stringArray            [Required] Region associated with the GCP provider. Minimum number of required regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::shared-subnet=<subnet-id>::instance-template=<instance-templates-for-YugabyteDB-nodes>". Region name and Shared subnet are required key-value pairs. Instance Template is optional. Each region can be added using separate --add-region flags.
      --edit-region stringArray           [Optional] Edit region details associated with the GCP provider. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::shared-subnet=<subnet-id>::instance-template=<instance-templates-for-YugabyteDB-nodes>". Region name is a required key-value pair. Shared subnet and Instance Template are optional. Each region needs to be modified using a separate --edit-region flag.
      --remove-region stringArray         [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --add-image-bundle stringArray      [Optional] Add Intel x86_64 image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::machine-image=<custom-ami>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle name, machine image and SSH user are required key-value pairs. The default SSH Port is 22. Default marks the image bundle as default for the provider. If default is not specified, the bundle will automatically be set as default if no other default bundle exists for the same architecture. Each image bundle can be added using separate --add-image-bundle flag.
      --edit-image-bundle stringArray     [Optional] Edit Intel x86_64 image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-uuid=<image-bundle-uuid>::machine-image=<custom-ami>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle UUID is a required key-value pair.Each image bundle can be added using separate --edit-image-bundle flag.
      --remove-image-bundle stringArray   [Optional] Image bundle UUID to be removed from the provider. Each bundle to be removed needs to be provided using a separate --remove-image-bundle definition.
      --airgap-install                    [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads.
      --ntp-servers stringArray           [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                              help for update
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
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update and some instance-type subcommands.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider gcp](yba_provider_gcp.md)	 - Manage a YugabyteDB Anywhere GCP provider

