---
title: YugabyteDB Managed CLI autocomplete
headerTitle: Configure autocompletion
linkTitle: Configure autocompletion
description: Configure autocompletion for YugabyteDB Managed CLI.
headcontent: Configure autocompletion for YugabyteDB Managed CLI
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-autocomplete
    parent: managed-cli
    weight: 20
type: docs
rightNav:
  hideH4: true
---

You can configure ybm command autocompletion for your shell using the `completion` command. For example:

```sh
ybm completion bash
```

This generates an autocompletion script for the specified shell. Available options are as follows:

- bash
- fish
- powershell
- zsh

## BASH

YBM CLI autocompletion depends on the 'bash-completion' package.

If not already installed, install it using your operating system's package manager.

To load completions in your current shell session, enter the following command:

```sh
source <(ybm completion bash)
```

To load completions for every new session, execute the following once:

```sh
#### for Linux:
ybm completion bash > /etc/bash_completion.d/ybm

#### for macOS:
ybm completion bash > $(brew --prefix)/etc/bash_completion.d/ybm
```

Start a new shell for the setup to take effect.

## FISH

To load completions in your current shell session, use the following command:

```sh
ybm completion fish | source
```

To load completions for every new session, execute the following command:

```sh
ybm completion fish > ~/.config/fish/completions/ybm.fish
```

Start a new shell for the setup to take effect.

## Powershell

To load completions in your current shell session, use the following command:

```sh
ybm completion powershell | Out-String | Invoke-Expression
```

To load completions for every new session, add the output of the preceding command
to your powershell profile.

## ZSH

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
