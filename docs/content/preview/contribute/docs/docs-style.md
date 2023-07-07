---
title: YugabyteDB documentation style guide
headerTitle: Docs style guide
linkTitle: Style guide
description: YugabyteDB documentation style guide
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: docs-style
    parent: docs
    weight: 2950
type: docs
rightNav:
  hideH4: true
---

The YugabyteDB documentation style is based on the [Microsoft style guide](https://docs.microsoft.com/en-us/style-guide/welcome/), with some input from [Apple's style guide](https://help.apple.com/applestyleguide/#/). We aim to automate as much as possible through Vale.

## Conventions

👉 There are always exceptions. Use your best judgement; clarity should always win over compliance with (at least _most of_) the rules.

### Voice

Yugabyte's documentation voice should be informal and authoritative. Speak (or rather, write) plainly, address the reader directly, and tell them what they need to know.

**Use second person.** Address the reader directly: "Before you start, make sure your VM is configured as follows."

**Active voice.** There are rare times when a passive construction is a lot less convoluted.

**Contractions.** They're good. Don't use them in a legal contract, but do use them most other places.

### Headings

**Sentence case.** "Provision your cluster", not "Provision Your Cluster". Obvious exceptions include proper nouns, product names, and more.

**Avoid numbered headings.** Occasionally, it might make sense to add numbers to headings, but in general, avoid it. It's a common error to delete a section and forget to re-number the ones that follow; and even if you do remember, manual re-numbering is just a nuisance. (This is the main reason for auto-numbering ordered lists, too!)

**Don't use terminal punctuation.** Headings don't need end punctuation, except run-in headings like these. And if your heading is a complete sentence, consider shortening it.

**Use imperative headings.** They're more of a call to action than gerunds. "Provision your cluster" makes it clear this is something _you the reader_ are going to do, rather than "Provisioning your cluster". Gerunds and other constructions will sometimes be clearer, so don't feel obligated to use an imperative where it really doesn't work.

### Links

Prefer markdown-style `[link text](link-target)` links over HTML tags. Markdown's endnote-style links are also fine to use. Hugo has its own curly-brace link syntax, but it's less friendly and doesn't seem to have many advantages in normal use.

### Code blocks

**Code language is required.** Every code block needs a language tag. Use `output` for all output, and append a language if you want to have source highlighting but still omit the Copy button (for example, `output.json`). The Hugo docs list [all language names](https://gohugo.io/content-management/syntax-highlighting/#list-of-chroma-highlighting-languages) you can use for fenced code blocks.

**Prompts may be necessary.** In a given procedure, the first code block should show the prompt; subsequent blocks can omit it, provided it doesn't impact clarity. Don't omit prompts (particularly shell prompts) if a procedure takes input in more than one way, such as an operation in ysqlsh followed by one in bash.

You can use the following attributes to modify code blocks:

- **Highlight bad code.** To highlight wrong coding practices, add the attribute `{.badcode}` to the code block. For example, ```sql{.badcode}```:

  ```sql{.badcode}
  ysqlsh> CREATE TABLE $$__banking__$$;
  ```

- **Remove the copy button.** To remove the Copy button from any code block, add the attribute `{.nocopy}` to the code block. For example, ```sql{.nocopy}```:

  ```sql{.nocopy}
  CREATE TABLE users(id INT PRIMARY KEY, name TEXT);
  ```

#### YSQL and YCQL code blocks

Tag YSQL code blocks as `plpgsql`, and YCQL code blocks as `cql`. The source highlighting differs slightly between the two.

### Admonitions

Use admonitions sparingly. They lose their effectiveness if they appear too often. Avoid multiple admonitions in a row, and in most cases don't place them immediately after a heading.

To insert an admonition (a tip, note, or warning box), see [Widgets and shortcodes](../widgets-and-shortcodes/#admonition-boxes).

## Markdown linting

Use `markdownlint` to find and fix Markdown problems locally before you push a commit. There's a command-line utility available through Homebrew, and there are also extensions available for several editors, including Visual Studio Code. Markdownlint's [rules are well-documented](https://github.com/DavidAnson/markdownlint/blob/main/doc/Rules.md).

## Prose linting with Vale

We aim to use [Vale](https://docs.errata.ai) to reinforce as many of the style-guide conventions as possible.

You can run Vale locally in two ways: in a supported editor, and from the command line. Either way you run it, it'll pick up the configuration from [`/docs/.vale.ini`](https://github.com/yugabyte/yugabyte-db/blob/master/docs/.vale.ini) in the main repository.

**To run Vale from the command line** (there are more options than this!):

1. `brew install vale`
1. `vale <filename>` or `vale <foldername>`

For example, `vale docs/` or `vale yb-docs-style-guide.md`.

**To run Vale in Visual Studio Code**:

1. `brew install vale` (Even running in the editor, you still need the CLI installed.)
1. In Visual Studio Code, find and install the `Vale` extension from errata-ai.
1. Configure the extension: check the Vale > Core > Use CLI checkbox.
1. Restart Visual Studio Code.

Installed in this way, Vale lints the current file every time you save it.
