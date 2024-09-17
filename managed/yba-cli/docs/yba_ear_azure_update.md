## yba ear azure update

Update a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration

### Synopsis

Update an Azure encryption at rest (EAR) configuration in YugabyteDB Anywhere

```
yba ear azure update [flags]
```

### Options

```
      --client-id string       [Optional] Update Azure Client ID.
      --tenant-id string       [Optional] Update Azure Tenant ID.
      --client-secret string   [Optional] Update Azure Secret Access Key. Required for Non Managed Identity based configurations. 
      --use-managed-identity   [Optional] Use Azure Managed Identity from the YugabyteDB Anywhere Host. EAR creation will fail on insufficient permissions on the host. (default false)
  -h, --help                   help for update
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear azure](yba_ear_azure.md)	 - Manage a YugabyteDB Anywhere Azure encryption at rest (EAR) configuration

