## yba universe security eit cert

Rotate certificates for a universe

### Synopsis

Rotate certificates for a universe. Rotation is supported on all certificate types (except for cert-manager on k8s)

```
yba universe security eit cert [flags]
```

### Examples

```
yba universe security eit cert --name <universe-name> --rotate-self-signed-client-cert
```

### Options

```
      --root-ca string                       [Optional] Root Certificate name.
      --client-root-ca string                [Optional] Client Root Certificate name.
      --rotate-self-signed-client-cert       [Optional] Rotates client certificate. Cannot rotate client certificate when root CA or client root CA are being rotated (default false)
      --rotate-self-signed-server-cert       [Optional] Rotates server certificate. Cannot rotate client certificate when root CA or client root CA are being rotated (default false)
      --root-and-client-root-ca-same         [Optional] Use same certificates for node to node and client to node communication. Set true for rotating server and client certificates and when root CA and client root CA are same. (default true)
      --delay-between-master-servers int32   [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32         [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
  -h, --help                                 help for cert
```

### Options inherited from parent commands

```
  -a, --apiToken string         YugabyteDB Anywhere api token.
      --config string           Config file, defaults to $HOME/.yba-cli.yaml
      --debug                   Use debug mode, same as --logLevel debug.
      --disable-color           Disable colors in output. (default false)
  -f, --force                   [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string             YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string         Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string             [Required] The name of the universe for the operation.
  -o, --output string           Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations        [Optional] Skip validations before running the CLI command.
      --timeout duration        Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --upgrade-option string   [Optional] Upgrade Options, defaults to Rolling. Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime). Only a "Non-Rolling" type of restart is allowed for TLS upgrade. (default "Rolling")
      --wait                    Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe security eit](yba_universe_security_eit.md)	 - Encryption-in-transit settings for a universe

