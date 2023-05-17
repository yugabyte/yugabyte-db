---
title: Install and configure ybm CLI
headerTitle: Install and configure
linkTitle: Install and configure
description: Install and configure ybm CLI.
headcontent: Install ybm, configure default settings, and set up autocompletion
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-overview
    parent: managed-cli
    weight: 50
type: docs
rightNav:
  hideH4: true
---

## Install ybm

To install ybm, download the appropriate binary from the ybm-cli GitHub project [releases](https://github.com/yugabyte/ybm-cli/releases).

On MacOS and Linux, you can also install ybm using [Homebrew](https://brew.sh) by entering the following command:

```sh
brew install yugabyte/tap/ybm
```

If you don't have a YugabyteDB Managed account yet, you can use the following command to bring up the sign up page:

```sh
ybm signup
```

For help, enter the following command:

```sh
ybm help
```

## Upgrade ybm

To upgrade ybm using Homebrew, enter the following commands:

```sh
brew update && brew upgrade ybm
```

## Configure ybm

Using ybm CLI requires providing, at minimum, an [API key](../../managed-apikeys/).

You can pass the key as a [flag](../managed-cli-reference/#global-flags) when running ybm commands. For example:

```sh
ybm cluster list --apiKey "eyJ..."
```

For convenience, you can configure ybm with a default API key as follows:

- Use the `auth` command to write the key to a YAML configuration file, as follows:

  ```sh
  ybm auth
  ```

  At the prompt, paste your API key and press Enter.

  By default, this writes the value to the file `.ybm-cli.yaml` under your `$HOME` directory.

  You can create multiple configuration files, and switch between them using the [--config](../managed-cli-reference/#global-flags) flag. You can add any of the other [global flags](../managed-cli-reference/#global-flags) to your configuration files.

- Using [environment variables](#environment-variables). Environment variables must begin with `YBM_`. For example:

  ```sh
  export YBM_APIKEY=AWERDFSSS
  ybm cluster list
  ```

If a value is set in an environment variable and a configuration file, the environment variable takes precedence. Setting the value using the flag takes precedence over both.

### Environment variables

You can set the following ybm environment variables.

| Variable | Description |
| :--- | :--- |
| YBM_APIKEY | The API key to use to authenticate to your YugabyteDB Managed account. |
| YBM_CI | Set to `true` to avoid outputting unnecessary log lines. |
| YBM_AWS_SECRET_KEY | AWS secret access key. For encryption at rest of AWS clusters. |
<!--| YBM_HOST | The host address of the cluster you are managing. By default, https is added to the host if no scheme is provided. |-->

## Autocompletion

You can configure command autocompletion for your shell using the `completion` command. For example:

```sh
ybm completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

### Bash

ybm CLI autocompletion depends on the 'bash-completion' package. If not already installed, install it using your operating system's package manager.

To load completions in your current shell session, enter the following command:

```sh
source <(ybm completion bash)
```

To load completions for every new session, execute the following command:

```sh
#### for Linux:
ybm completion bash > /etc/bash_completion.d/ybm

#### for macOS:
ybm completion bash > $(brew --prefix)/etc/bash_completion.d/ybm
```

Start a new shell for the setup to take effect.

### fish

To load completions in your current shell session, use the following command:

```sh
ybm completion fish | source
```

To load completions for every new session, execute the following command:

```sh
ybm completion fish > ~/.config/fish/completions/ybm.fish
```

Start a new shell for the setup to take effect.

### PowerShell

To load completions in your current shell session, use the following command:

```sh
ybm completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the preceding command to your PowerShell profile.

### Zsh

If shell completion is not already enabled in your environment, you can turn it on by running the following command:

```sh
echo "autoload -U compinit; compinit" >> .zshrc
```

To load completions in your current shell session, enter the following command:

```sh
source <(ybm completion zsh); compdef _ybm ybm
```

To load completions for every new session, execute the following:

```sh
#### for Linux:
ybm completion zsh > "${fpath[1]}/_ybm"

#### for macOS:
ybm completion zsh > $(brew --prefix)/share/zsh/site-functions/_ybm
```

Start a new shell for the setup to take effect.
