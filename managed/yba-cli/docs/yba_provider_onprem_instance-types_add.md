## yba provider onprem instance-types add

Add an instance type to YugabyteDB Anywhere on-premises provider

### Synopsis

Add an instance type to YugabyteDB Anywhere on-premises provider

```
yba provider onprem instance-types add [flags]
```

### Options

```
      --instance-type-name string   [Required] Instance type name.
      --volume stringArray          [Required] Volumes associated per node of an instance type. Minimum number of required volumes = 1. Provide the following comma separated fields as key-value pairs:"type=<volume-type>,size=<volume-size>,mount-points="<quoted-comma-separated-mount-points>"". mount-points is a required key-value. Volume type (Defaults to SSD, Allowed values: EBS, SSD, HDD, NVME) and Volume size (Defaults to 100) are optional. Each volume needs to be added using a separate --volume flag.
      --mem-size float              [Optional] Memory size of the node in GB. (default 8)
      --num-cores float             [Optional] Number of cores per node. (default 4)
      --tenancy string              [Optional] Tenancy of the nodes of this type. Allowed values (case sensitive): Shared, Dedicated, Host.
  -h, --help                        help for add
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

* [yba provider onprem instance-types](yba_provider_onprem_instance-types.md)	 - Manage YugabyteDB Anywhere onprem instance types

