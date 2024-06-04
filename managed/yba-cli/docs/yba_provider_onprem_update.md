## yba provider onprem update

Update an On-premises YugabyteDB Anywhere provider

### Synopsis

Update an On-premises provider in YugabyteDB Anywhere

```
yba provider onprem update [flags]
```

### Options

```
      --new-name string             [Optional] Updating provider name.
      --add-region stringArray      [Optional] Add region associated with the On-premises provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,latitude=<latitude>,longitude=<longitude>". Region name is a required key-value. Latitude and Longitude are optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray        [Optional] Zone associated to the On-premises Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>". Zone name and Region name are required values. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided per region.Each zone needs to be added using a separate --add-zone flag.
      --remove-region stringArray   [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray     [Optional] Remove zone associated to the On-premises Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --edit-region stringArray     [Optional] Edit region details associated with the On-premises provider. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,latitude=<latitude>,longitude=<longitude>". Region name is a required key-value. Latitude and Longitude are optional. Each region needs to be modified using a separate --edit-region flag.
      --ssh-user string             [Optional] Updating SSH User to access the YugabyteDB nodes.
      --ssh-port int                [Optional] Updating SSH Port to access the YugabyteDB nodes.
      --airgap-install              [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --passwordless-sudo-access    [Optional] Can sudo actions be carried out by user without a password.
      --skip-provisioning           [Optional] Set to true if YugabyteDB nodes have been prepared manually, set to false to provision during universe creation.
      --install-node-exporter       [Optional] Install Node exporter.
      --node-exporter-user string   [Optional] Node Exporter User.
      --node-exporter-port int      [Optional] Node Exporter Port.
      --ntp-servers stringArray     [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
      --yb-home-dir string          [Optional] YB Home directory.
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
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, instance-types and nodes.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider onprem](yba_provider_onprem.md)	 - Manage a YugabyteDB Anywhere on-premises provider

