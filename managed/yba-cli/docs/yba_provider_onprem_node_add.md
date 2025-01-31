## yba provider onprem node add

Add a node instance to YugabyteDB Anywhere on-premises provider

### Synopsis

Add a node instance to YugabyteDB Anywhere on-premises provider

```
yba provider onprem node add [flags]
```

### Examples

```
yba provider onprem add --name <provider-name> \
	--ip <node-ip> --instance-type <instance-type> \
	--region <region> --zone <zone>
```

### Options

```
      --instance-type string       [Required] Instance type of the node as describe in the provider.
      --ip string                  [Required] IP address of the node instance.
      --region string              [Required] Region name of the node.
      --zone string                [Required] Zone name of the node.
      --node-name string           [Optional] Node name given by the user.
      --ssh-user string            [Optional] SSH user to access the node instances.
      --node-configs stringArray   [Optional] Node configurations. Provide the following double colon (::) separated fields as key-value pairs: "type=<type>::value=<value>". Each config needs to be added using a separate --node-configs flag. Example: --node-configs type=S3CMD::value=<value> --node-configs type=CPU_CORES::value=<value>
  -h, --help                       help for add
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, instance-type and node.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider onprem node](yba_provider_onprem_node.md)	 - Manage YugabyteDB Anywhere onprem node instances

