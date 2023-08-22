## yba completion powershell

Generate the autocompletion script for powershell

### Synopsis

Generate the autocompletion script for powershell.

To load completions in your current shell session:

	yba completion powershell | Out-String | Invoke-Expression

To load completions for every new session, add the output of the above command
to your powershell profile.


```
yba completion powershell [flags]
```

### Options

```
  -h, --help              help for powershell
      --no-descriptions   disable completion descriptions
```

### Options inherited from parent commands

```
  -a, --apiKey string      YugabyteDB Anywhere api key
      --config string      config file, default is $HOME/.yba-cli.yaml
      --debug              use debug mode, same as --logLevel debug
  -H, --host string        YugabyteDB Anywhere Host, default to http://localhost:9000
  -l, --logLevel string    select the desired log level format, default to info
      --no-color           disable colors in output , default to false
  -o, --output string      select the desired output format (table, json, pretty), default to table
      --timeout duration   wait command timeout,example: 5m, 1h. (default 168h0m0s)
      --wait               wait until the task is completed, otherwise it will exit immediately, default to true
```

### SEE ALSO

* [yba completion](yba_completion.md)	 - Generate the autocompletion script for the specified shell

