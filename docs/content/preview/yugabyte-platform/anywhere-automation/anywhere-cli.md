---
title: YugabyteDB Anywhere CLI
headerTitle: YugabyteDB Anywhere CLI
linkTitle: CLI
description: Use YugabyteDB Anywhere CLI to create and manage resources in YBA.
headcontent: Install the CLI, configure default settings, and set up autocompletion
techPreview: /preview/releases/versioning/#feature-availability
menu:
  preview_yugabyte-platform:
    parent: anywhere-automation
    identifier: anywhere-cli
    weight: 50
type: docs
rightNav:
  hideH4: true
---

The [YugabyteDB Anywhere (YBA) Command Line Interface](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba.md) (CLI) is an open source tool that enables you to interact with YBA using commands from your shell. With minimal configuration, you can start running commands from the command line that implement functionality equivalent to that provided by the browser-based YBA interface.

The CLI can only be used with YugabyteDB Anywhere v2024.1 or later.

The CLI is in [Tech Preview](/preview/releases/versioning/#feature-availability). Commands and options may change before {{<badge/ga>}}.

## Install CLI

Your YugabyteDB Anywhere installation includes the CLI binary, located in `/opt/yugabyte/yugaware/yba-cli`. The CLI is available for following architectures:

- Linux AMD64 and ARM64
- Darwin AMD64 and ARM64

Copy the appropriate binary to your local machine. Note that the CLI can only be used with the same or earlier version of YBA.

For help, enter the following command:

```sh
yba help
```

### Use CLI

Using the CLI requires providing, at minimum, an [API token](../#authentication).

You can pass the token as a flag when running CLI commands. For example:

```sh
yba universe list --apiToken "eyJ..."
```

For convenience, you can configure the CLI with a default API token as follows:

- Use the `auth` command to write the token to a CLI configuration file, as follows:

  ```sh
  yba auth
  ```

  At the prompt, paste your API token and press Enter.

  By default, this writes the value to the CLI configuration file `$HOME/.yba-cli.yaml`.

  You can create multiple configuration files, and switch between them using the [--config](#global-flags) flag. You can add any of the other [global flags](#global-flags) to your configuration files.

- Using [environment variables](#environment-variables). Environment variables must begin with `YBA_`. For example:

  ```sh
  export YBA_APITOKEN=AWERDFSSS
  yba universe list
  ```

If a value is set in an environment variable and a configuration file, the environment variable takes precedence. Setting the value using the flag takes precedence over both.

### Environment variables

You can set the following CLI environment variables.

| Variable | Description |
| :--- | :--- |
| YBA_APITOKEN | The API token to use to authenticate to your YugabyteDB Managed account. |
| YBA_CI | Set to `true` to avoid outputting unnecessary log lines. |
| YBA_HOST | The host address of the universe you are managing. By default, https is added to the host if no scheme is provided. |

## Syntax

```sh
yba [-h] [ <resource> ] [ <command> ] [ <flags> ]
```

- resource: resource to be changed
- command: command to run
- flags: one or more flags, separated by spaces.

For example:

```sh
yba universe list
```

### Online help

Access command-line help for the CLI by running the following command:

```sh
yba help
```

For help with specific resource commands, use the `--help` or `-h` flag, in the following form:

```sh
yba [ resource ] [ command ] -h
```

For example, to print the command-line help for the cluster `create` command, run the following:

```sh
yba universe list -h
```

Print the version of the CLI:

```sh
yba --version
```

## Commands

You can manage the following resources:

| Resource | Description |
| :--- | :--- |
| [provider](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_provider.md) | Manage provider configurations. |
| [storage-config](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_storage-config.md) | Manage storage configurations. |
| [task](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_task.md) | Manage tasks. |
| [universe](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_universe.md) | Manage universes. |
| [yb-db-version](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_yb-db-version.md) | Manage YugabyteDB releases. |

Use the following commands to configure the CLI:

| Resource | Description |
| :--- | :--- |
| [auth](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_auth.md) | Write API token to a configuration file. |
| [completion](#autocompletion) | Configure autocompletion for Bash, Fish, PowerShell, and Zsh |
| [login](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_login.md) | Connect to YugabyteDB Anywhere host machine using email and password. |
| [register](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba_register.md) | Register a YugabyteDB Anywhere customer. |

### Global flags

The following flags can be passed in with any command. These flags can also be added to your configuration file (see [Use CLI](#use-cli)).

-a, --apiToken string
: YugabyteDB Anywhere account API Key.

--config string
: Configuration file (default is `$HOME/.yba-cli.yaml`).

--debug
: Use debug mode, same as `--logLevel debug`.

--disable-color
: Disable colors in output. `true` or `false` (default).

-l, --logLevel string
: Specify the desired level of logging. `debug` or `info` (default).

-o, --output string
: Specify the desired output format. `table` (default), `json`, or `pretty`.

--timeout duration
: Wait command timeout. For example, 5m, 1h. Default is 168h0m0s.

--wait
: For long-running commands such as creating or deleting a cluster, you can use the `--wait` flag to display progress in the shell. `true` or `false` (default). For example:

```sh
yba universe delete \
    --name=test-universe \
    --wait
```

If you are using the CLI with the `--wait` flag in your CI system, you can set the environment variable `YBA_CI` to `true` to avoid generating unnecessary log lines.

## Autocompletion

You can configure command autocompletion for your shell using the `completion` command. For example:

```sh
yba completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

### Bash

yba CLI autocompletion depends on the 'bash-completion' package. If not already installed, install it using your operating system's package manager.

To load completions in your current shell session, enter the following command:

```sh
source <(yba completion bash)
```

To load completions for every new session, execute the following command:

```sh
#### for Linux:
yba completion bash > /etc/bash_completion.d/yba

#### for macOS:
yba completion bash > $(brew --prefix)/etc/bash_completion.d/yba
```

Start a new shell for the setup to take effect.

### fish

To load completions in your current shell session, use the following command:

```sh
yba completion fish | source
```

To load completions for every new session, execute the following command:

```sh
yba completion fish > ~/.config/fish/completions/yba.fish
```

Start a new shell for the setup to take effect.

### PowerShell

To load completions in your current shell session, use the following command:

```sh
yba completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the preceding command to your PowerShell profile.

### Zsh

If shell completion is not already enabled in your environment, you can turn it on by running the following command:

```sh
echo "autoload -U compinit; compinit" >> .zshrc
```

To load completions in your current shell session, enter the following command:

```sh
source <(yba completion zsh); compdef _yba yba
```

To load completions for every new session, execute the following:

```sh
#### for Linux:
yba completion zsh > "${fpath[1]}/_yba"

#### for macOS:
yba completion zsh > $(brew --prefix)/share/zsh/site-functions/_yba
```

Start a new shell for the setup to take effect.
