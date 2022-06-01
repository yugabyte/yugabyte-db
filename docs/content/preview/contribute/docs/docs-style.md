---
title: YugabyteDB documentation style guide
headerTitle: Docs style guide
linkTitle: Style guide
description: YugabyteDB documentation style guide
image: /images/section_icons/index/quick_start.png
type: page
menu:
  preview:
    identifier: docs-style
    parent: docs
    weight: 2950
isTocNested: true
showAsideToc: true
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

**Sentence case.** For example, "Provision your cluster", not "Provision Your Cluster". Obvious exceptions include proper nouns, product names, and more.

**Avoid numbered headings.** Occasionally, it might make sense to add numbers to headings, but in general, avoid it. It's a common error to delete a section and forget to re-number the ones that follow; and even if you do remember, manual re-numbering is just a nuisance. (This is the main reason for auto-numbering ordered lists, too!)

**Don't use terminal punctuation.** Headings don't need end punctuation, except run-in headings like these. And if your heading is a complete sentence, consider shortening it.

**Use imperative headings.** They're more of a call to action than gerunds. "Provision your cluster" makes it clear this is something _you the reader_ are going to do, rather than "Provisioning your cluster". Gerunds and other constructions will sometimes be clearer, so don't feel obligated to use an imperative where it really doesn't work.

### Links

Prefer markdown-style `[link text](link-target)` links over HTML tags. Markdown's endnote-style links are also fine to use. Hugo has its own curly-brace link syntax, but it's less friendly and doesn't seem to have any advantages in normal use.

### Including content from other files

The [includeCode](#includecode) and [includeFile](#includefile) shortcodes insert the contents of a file as plain text (includeFile can optionally add source-highlighting), while [includeMarkdown](#includemarkdown) inserts the contents of a file _and renders it as markdown_.

`includeCode` and `includeFile` both strip trailing whitespace from the input file.

Look in [sample-files/include-test.md](sample-files/include-test.md) for examples of how to use both shortcodes. Both shortcodes are defined in [/docs/layouts/shortcodes](https://github.com/yugabyte/yugabyte-db/tree/master/docs/layouts/shortcodes/) in the main repository.

#### includeCode

Because it doesn't create its own code block, you can use this shortcode to build a code block from several sources.

The base path is `/docs/static/`.

**Call `includeCode`** in a fenced code block:

````markdown
```sql
{{%/* includeCode file="code-samples/include.sql" */%}}
```
````

**To nest the code block**, tell the shortcode how many spaces to indent:

````markdown
1. To do this thing, use this code:

    ```sql
    {{%/* includeCode file="code-samples/include.sql" spaces=4 */%}}
    ```
````

**To specify highlighting options**, do so on the fenced code block. This is a Hugo feature, not part of the shortcode. For example, add a highlight to lines 1 and 7-10:

````markdown
```sql {hl_lines=[1,"7-10"]}
{{%/* includeCode file="code-samples/include.sql" */%}}
```
````

> For more information on highlight options: <https://gohugo.io/content-management/syntax-highlighting/#highlighting-in-code-fences>

#### includeFile

The `includeFile` shortcode infers the code language from the filename extension (or `output` if there's no extension) and creates its own code block.

The base path is `/docs/static/`.

**Call `includeFile`** on a line of its own:

```go
{{</* includeFile file="code-samples/include.sql" */>}}
```

**To nest the code block**, indent the shortcode:

```go
    {{</* includeFile file="code-samples/include.sql" */>}}
```

**To specify a code language** and override the default:

```go
{{</* includeFile file="code-samples/include.sql" lang="sql" */>}}
```

**To specify highlighting options**:

```go
{{</* includeFile file="code-samples/include.sql" hl_options="hl_lines=1 7-10" */>}}
```

> CAREFUL! `hl_lines` takes a different form here than when you're specifying it on a fenced block: no comma, no quotes: `hl_options="hl_lines=1 7-10"`
>
> For more information on highlight options: <https://gohugo.io/content-management/syntax-highlighting/#highlight-shortcode>

#### includeMarkdown

Inserts the contents of a markdown file, rendered as part of the calling page. We use this primarily for syntax diagrams.

### Code blocks

**Code language is required.** Every code block needs a language tag. Use `output` for all output, and append a language if you want to have source highlighting but still omit the Copy button (for example, `output.json`). The Hugo docs list [all language names](https://gohugo.io/content-management/syntax-highlighting/#list-of-chroma-highlighting-languages) you can use for fenced code blocks.

**Prompts may be necessary.** In a given procedure, the first code block should show the prompt; subsequent blocks can omit it, provided it doesn't impact clarity. Don't omit prompts (particularly shell prompts) if a procedure takes input in more than one way, such as an operation in ysqlsh followed by one in bash.

#### YSQL and YCQL code blocks

Tag YSQL code blocks as `sql`, and YCQL code blocks as `cql`. The source highlighting differs slightly between the two.

## Markdown linting

Use `markdownlint` to find and fix Markdown problems locally before you push a commit. There's a command-line utility available through Homebrew, and there are also extensions available for several editors, including Visual Studio Code. Markdownlint's [rules are well-documented](https://github.com/DavidAnson/markdownlint/blob/main/doc/Rules.md).

## Prose linting with Vale

We aim to use [Vale](https://docs.errata.ai) to reinforce as many of the style-guide conventions as possible.

You can run Vale locally in two ways: in a supported editor, and from the command line. Either way you run it, it'll pick up the configuration from [`/docs/.vale.ini`](https://github.com/polarweasel/yugabyte-db/blob/master/docs/.vale.ini) in the main repository.

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
