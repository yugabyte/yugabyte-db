## yba user

Manage YugabyteDB Anywhere users

### Synopsis

Manage YugabyteDB Anywhere users

```
yba user [flags]
```

### Options

```
  -h, --help   help for user
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba user create](yba_user_create.md)	 - Create a YugabyteDB Anywhere user
* [yba user delete](yba_user_delete.md)	 - Delete a YugabyteDB Anywhere user
* [yba user describe](yba_user_describe.md)	 - Describe a YugabyteDB Anywhere user
* [yba user list](yba_user_list.md)	 - List YugabyteDB Anywhere users
* [yba user reset-password](yba_user_reset-password.md)	 - Reset password of currently logged in YugabyteDB Anywhere user
* [yba user update](yba_user_update.md)	 - Update a YugabyteDB Anywhere user

