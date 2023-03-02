---
title: YugabyteDB Managed quick start
headerTitle: Page with elements
linkTitle: Page with elements
headcontent: Just about every page element on a single page
description: YugabyteDB Documentation page elements on a single page.
layout: single
type: docs
menu:
  preview:
    identifier: all-page-elements
    parent: docs-edit
    weight: 9000
---

This page demonstrates styles and widgets used for the YugabyteDB Documentation site.

## Tab widgets

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../all-page-elements/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Icon">
      Top-level Tab Widget
    </a>
  </li>
  <li>
    <a href="../all-page-elements/" class="nav-link">
      <img src="/icons/server.svg" alt="Icon">
      Tab 2
    </a>
  </li>
</ul>

This is a top-level tab widget, that uses different files for each tab. Everything that follows is in this tab's file. If you change tabs, you get a whole new page. This style is possibly only used for the quick start.

### Second tab widget style

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../all-page-elements/" class="nav-link">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="../all-page-elements/" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="../all-page-elements/" class="nav-link">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="../all-page-elements/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

This is a second-level tab widget, that uses different files for each tab - same as the one above, just styled differently. We should be using this style instead of the buttons for this type of tab. Many pages need to be changed to do that.

Everything that follows is in this tab's file. If you change tabs, you get a whole new page.

### In-page tab widget

This tab widget doesn't use separate files to fill in the content and then link between. Here the content is placed inside the `tabpane` shortcode.

{{< tabpane text=true >}}

  {{% tab header="Java" lang="java" %}}

Another tab style; here all the tab contents are in-page.

### Headings inside this widget don't show up in RightNav

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Set the following configuration parameters:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).

    - Save the file.

1. Start the application.

    ```sh
    $ java -cp target/yugabyte-simple-java-app-1.0-SNAPSHOT.jar SampleApp
    ```

The end of this tab.

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

The contents of the next tab. You can keep adding tabs in similar fashion.

### Headings inside this widget don't show up in RightNav

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-go-app.git && cd yugabyte-simple-go-app
    ```

1. Provide connection parameters.

  {{% /tab %}}

{{< /tabpane >}}

### Pills

These pills automatically re-flow based on page width; just keep adding list items. Use these pills for landing pages or as an alternative to tabs.

<ul class="nav yb-pills">

  <li>
    <a href="../../../develop/build-apps/java/cloud-ysql-yb-jdbc/" class="orange">
      <i class="fa-brands fa-java"></i>
      Java
    </a>
  </li>

  <li>
    <a href="../../../develop/build-apps/go/cloud-ysql-go/" class="orange">
      <i class="fa-brands fa-golang"></i>
      Go
    </a>
  </li>

  <li>
    <a href="../../../develop/build-apps/python/cloud-ysql-python/" class="orange">
      <i class="fa-brands fa-python"></i>
      Python
    </a>
  </li>

  <li>
    <a href="../../../develop/build-apps/nodejs/cloud-ysql-node/" class="orange">
      <i class="fa-brands fa-node-js"></i>
      NodeJS
    </a>
  </li>

</ul>

## Table

The following is a basic markdown table.

| Table | A column |
| :---- | :------- |
| A row | Another column in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |
| Another row | Another row in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |
| Another row | Another column in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |

## Notes and blockquote

An ordinary paragraph.

>**Blockquote**
>
>Blockquote text
>
>Some more blockquote text.

{{< note title="Note" >}}

This is an actual Note. Pay special attention to this thing, maybe.

{{< /note >}}

{{< tip title="Tip" >}}

This is an actual Tip. Maybe there's a cool alternative to what you are doing.

{{< /tip >}}

{{< warning title="Warning" >}}

This is an actual Warning. Whatever you are doing may threaten your data if you do it wrong.

{{< /warning >}}

## Images and video

What follows is an image:

![Connect using cloud shell](/images/yb-cloud/cloud-connect-shell.gif)

What follows is an embedded YouTube video:

{{< youtube id="qYMcNzWotkI" title="Deploy a fault tolerant cluster in YugabyteDB Managed" >}}

## Lists

What follows are some numbered steps, some code, text indented under a step:

1. Numbered list with indented lists.

    1. Second level numbered list indent.

    1. Second level numbered list indent.

        1. Third level numbered list indent.

        1. Third level numbered list indent.

1. Second list item. Some text in **bold**. Some text in _italics_.

1. Third list item.

    Indented text under a list.

    ```output
    Some code
    ```

1. Fourth list item.

Bulleted lists with levels of indent:

- Bulleted list with indented lists.

  - Second level Bulleted list indent.

  - Second level Bulleted list indent.

    - Third level Bulleted list indent.

    - Third level Bulleted list indent.

### Heading 3

#### Heading 4

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Managed clusters using your favorite programming language.

Some bullets:

- a cluster deployed in YugabyteDB Managed.
- the cluster CA certificate; YugabyteDB Managed uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](../../../develop/build-apps/cloud-add-ip/).

## Glossary entries

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list.

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list.
: Another paragraph in the definition.

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list.
: Another paragraph in the definition.

## Details tag

The details HTML tag is used to create an interactive widget that the user can open and close. By default, the widget is closed. When open, it expands, and displays the contents.

<details>
  <summary>Tiresomely long code block</summary>

These contents might be a very long bit of code or somesuch.

</details>

## Syntax documentation

```sh
ysqlsh [ <option>...] [ <dbname> [ <username> ]]
```

### Default flags

When you open `ysqlsh`, the following default flags (aka flags) are set so that the user doesn't have to specify them.

- host: `-h 127.0.0.1`
- port: `-p 5433`
- user: `-U yugabyte`

## Horizontal rule

Once in awhile, there is a horizontal rule.

---

## Flags

Flags are documented often using these heading 5

##### heading 5

The heading 5 doesn't show up in right navigation. We probably wouldn't want it to, as there can be dozens of these flags on some pages.

Heading 5 is probably used so that we can still deep-link to the flag.

##### -F, --this-is-a-flag

This documentation isn't that easy to scan, as the headings for the option don't look much different from the explanatory text in the paragraphs that follow the heading 5.

##### -b, --echo-errors

Maybe if we had a style to indent the content under the heading.

##### -c *command*, --command=*command*

Specifies that `ysqlsh` is to execute the given command string, *command*. This flag can be repeated and combined in any order with the `-f` flag. When either `-c` or `-f` is specified, `ysqlsh` doesn't read commands from standard input; instead it terminates after processing all the `-c` and `-f` flags in sequence.

The command (*command*) must be either a command string that is completely parsable by the server (that is, it contains no `ysqlsh`-specific features), or a single backslash (`\`) command. Thus, you cannot mix SQL and `ysqlsh` meta-commands in a `-c` flag. To achieve that, you could use repeated `-c` flags or pipe the string into `ysqlsh`, for example:

```plpgsql
ysqlsh -c '\x' -c 'SELECT * FROM foo;'
```

or

```plpgsql
echo '\x \\ SELECT * FROM foo;' | ./bin/ysqlsh
```

(`\\` is the separator meta-command.)

Because of this behavior, putting more than one SQL statement in a single `-c` string often has unexpected results. It's better to use repeated `-c` commands or feed multiple commands to `ysqlsh`'s standard input, either using `echo` as illustrated above, or using a shell here-document, for example:

```plpgsql
./bin/ysqlsh<<EOF
\x
SELECT * FROM foo;
EOF
```

## Meta-commands

Like the Flags, there are dozens of Meta-commands, so again heading 5 is employed.

Documentation of some Meta-commands can go on for paragraphs and be very involved. One command has a separate set of options that is documented in a separate section, with its own set of heading 5s.

### Reference

The following meta-commands are available.

##### \a

If the current table output format is unaligned, it is switched to aligned. If it isn't unaligned, it is set to unaligned. This command is kept for backwards compatibility. See [\pset](#pset-option-value) for a more general solution.

##### \c, \connect [ -reuse-previous=on|off ] [ *dbname* [ *username* ] [ *host* ] [ *port* ] | *conninfo* ]

Establishes a new connection to a YugabyteDB server. The connection parameters to use can be specified either using a positional syntax, or using *conninfo* connection strings.

If the new connection is successfully made, the previous connection is closed. If the connection attempt failed (wrong user name, access denied, etc.), the previous connection is only kept if `ysqlsh` is in interactive mode. When executing a non-interactive script, processing immediately stops with an error. This distinction was chosen as a user convenience against typos on the one hand, and a safety mechanism that scripts aren't accidentally acting on the wrong database on the other hand.

Examples:

```plpgsql
=> \c mydb myuser host.dom 6432
=> \c service=foo
=> \c "host=localhost port=5432 dbname=mydb connect_timeout=10 sslmode=disable"
=> \c postgresql://tom@localhost/mydb?application_name=myapp
\C [ title ]
```

Sets the title of any tables being printed as the result of a query or unset any such title. This command is equivalent to [\pset title](#title-or-c). (The name of this command derives from "caption", as it was previously only used to set the caption in an HTML table.)

## API Syntax

API docs have these Grammar/Diagram pairs. They use includeMarkdown codes to fetch the contents from a markdown file in another location.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../api/ysql/syntax_resources/the-sql-language/statements/copy_from,copy_to,copy_option.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../api/ysql/syntax_resources/the-sql-language/statements/copy_from,copy_to,copy_option.diagram.md" %}}
  </div>
</div>
