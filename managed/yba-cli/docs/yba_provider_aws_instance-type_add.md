## yba provider aws instance-type add

Add an instance type to YugabyteDB Anywhere AWS provider

### Synopsis

Add an instance type to YugabyteDB Anywhere AWS provider

```
yba provider aws instance-type add [flags]
```

### Examples

```
yba provider aws instance-type add \
	--name <provider-name> --instance-type-name <instance-type>\
	--volume mount-points=<mount-point>::size=<size>::type=<volume-type>
```

### Options

```
      --instance-type-name string   [Required] Instance type name.
      --arch string                 [Optional] Architecture of the instance type. Allowed values: x86_64, arm64/aarch64. (default "x86_64")
      --volume stringArray          [Optional] Volumes associated per node of an instance type. Provide the following double colon (::) separated fields as key-value pairs: "type=<volume-type>::size=<volume-size>::mount-points=<comma-separated-mount-points>". Mount points is a required key-value. Volume type (Defaults to EBS, Allowed values: EBS, SSD, HDD, NVME) and Volume size (Defaults to 100) are optional. Each volume needs to be added using a separate --volume flag.
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
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update, and some instance-type subcommands.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider aws instance-type](yba_provider_aws_instance-type.md)	 - Manage YugabyteDB Anywhere AWS instance types

