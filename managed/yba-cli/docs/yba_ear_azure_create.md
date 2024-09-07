## yba ear azure create

Create a YugabyteDB Anywhere Azure encryption at rest configuration

### Synopsis

Create an Azure encryption at rest configuration in YugabyteDB Anywhere

```
yba ear azure create [flags]
```

### Options

```
      --client-id string       Azure Client ID. Can also be set using environment variable AZURE_CLIENT_ID.
      --tenant-id string       Azure Tenant ID. Can also be set using environment variable AZURE_TENANT_ID.
      --client-secret string   Azure Secret Access Key. Required for Non Managed Identity based configurations. Can also be set using environment variable AZURE_CLIENT_SECRET.
      --use-managed-identity   [Optional] Use Azure Managed Identity from the YugabyteDB Anywhere Host. EAR creation will fail on insufficient permissions on the host. (default false)
      --vault-url string       [Required] Azure Vault URL.
      --key-name string        [Required] Azure Key Name.If master key with same name already exists then it will be used, else a new one will be created automatically.
      --key-algorithm string   [Optional] Azure Key Algorithm. Allowed values (case sensitive): RSA (default "RSA")
      --key-size int           [Optional] Azure Key Size. Allowed values per algorithm: RSA(Default:2048, 3072, 4096)
  -h, --help                   help for create
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

