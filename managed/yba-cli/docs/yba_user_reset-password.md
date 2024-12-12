## yba user reset-password

Reset password of currently logged in YugabyteDB Anywhere user

### Synopsis

Reset password of currently logged in user in YugabyteDB Anywhere

```
yba user reset-password [flags]
```

### Examples

```
yba user reset-password \
	 --current-password <current-password> --new-password <new-password>
```

### Options

```
      --current-password string   [Required] The current password of the user.
      --new-password string       [Required] The new password of the user.
  -h, --help                      help for reset-password
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba user](yba_user.md)	 - Manage YugabyteDB Anywhere users

