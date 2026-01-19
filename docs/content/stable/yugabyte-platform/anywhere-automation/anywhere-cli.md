---
title: YugabyteDB Anywhere CLI (yba)
headerTitle: YugabyteDB Anywhere CLI
linkTitle: yba CLI
description: Use YugabyteDB Anywhere CLI to create and manage resources in YBA.
headcontent: Install the CLI, configure default settings, and set up autocompletion
menu:
  stable_yugabyte-platform:
    parent: anywhere-automation
    identifier: anywhere-cli
    weight: 50
type: docs
rightNav:
  hideH4: true
---

The [YugabyteDB Anywhere Command Line Interface](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/) (CLI) is an open source tool that enables you to interact with YugabyteDB Anywhere using commands from your shell. With minimal configuration, you can start running commands from the command line that implement functionality equivalent to that provided by the browser-based YugabyteDB Anywhere interface.

Reference documentation for yba CLI commands and their flags is available in the [Docs folder](https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/docs/yba.md) of the GitHub repository.

The CLI can only be used with YugabyteDB Anywhere v2024.1 or later.

## Install CLI

You can install and use the YugabyteDB Anywhere CLI in two ways.

### Option 1: Download and extract

1. Download and extract the YugabyteDB Anywhere CLI by entering the following commands:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable" format="long">}}/yba_cli-{{<yb-version     version="stable" format="build">}}-linux-amd64.tar.gz
    tar -xf yba_cli-{{<yb-version version="stable" format="build">}}-linux-amd64.tar.gz
    cd yba_cli-{{<yb-version version="stable" format="build">}}-linux-amd64/
    ```

1. Verify that the package is available:

    ```sh
    ./yba help
    ```

### Option 2: Use included binary

The CLI binary is already included in your YugabyteDB Anywhere installation at the following location:

```sh
/opt/ybanywhere/software/active/yb-platform/yugaware/yba-cli
```

The CLI is available for the following architectures:

- Linux AMD64 and ARM64
- Darwin AMD64 and ARM64

Copy the appropriate binary to your local machine. Note that the CLI can only be used with the same or earlier version of YBA.

### Need Help?

For CLI commands and options, run:

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

  By default, this writes the value to the CLI configuration file `$HOME/.yba-cli/.yba-cli.yaml`.

  You can create multiple configuration files, and switch between them using the `--config` flag, or `--directory` flag if you want to maintain the configuration files in a directory. See [Global flags](#global-flags) for more details.

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
| YBA_APITOKEN | The API token to use to authenticate to your YugabyteDB Aeon account. |
| YBA_CI | Set to `true` to avoid outputting unnecessary log lines. |
| YBA_FF_PREVIEW | Set to `true` to access commands and flags that are considered {{<tags/feature/tp>}}. |
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

### Global flags

The following flags can be passed in with any command. These flags can also be added to your configuration file (see [Use CLI](#use-cli)).

-a, --apiToken string
: YugabyteDB Anywhere account API Key.

--config string
: Configuration file (default is `$HOME/.yba-cli.yaml`).

--directory string
: Directory containing YBA CLI configuration and generated files. If specified, the CLI looks for a configuration file named `.yba-cli.yaml` in this directory. Default is `$HOME/.yba-cli/`.

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
