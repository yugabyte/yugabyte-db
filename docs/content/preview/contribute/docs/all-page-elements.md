---
title: YugabyteDB Managed quick start
headerTitle: Page with elements
linkTitle: Page with elements
headcontent: Sign up for YugabyteDB Managed and create a free Sandbox cluster
description: Get started using YugabyteDB Managed in less than five minutes.
layout: single
type: docs
menu:
  preview:
    identifier: all-page-elements
    parent: docs
    weight: 9000
---

<div class="custom-tabs tabs-style-2">
  <ul class="tabs-name">
    <li class="active">
      <a href="../all-page-elements/" class="nav-link">
        Top-level Tab Widget
      </a>
    </li>
    <li>
      <a href="../quick-start/" class="nav-link">
        Use a local cluster
      </a>
    </li>
  </ul>
</div>

This is a top-level tab widget, that uses different files for each tab. Everything that follows is in this tab's file. If you change tabs, you get a whole new page. This style is possibly only used for the quick start.

<div class="custom-tabs tabs-style-1">
  <ul class="tabs-name">
    <li class="active">
      <a href="../all-page-elements/" class="nav-link">
        <i class="fab fa-apple" aria-hidden="true"></i>
        macOS
      </a>
    </li>
    <li>
      <a href="../quick-start/linux/" class="nav-link">
        <i class="fab fa-linux" aria-hidden="true"></i>
        Linux
      </a>
    </li>
    <li>
      <a href="../quick-start/docker/" class="nav-link">
        <i class="fab fa-docker" aria-hidden="true"></i>
        Docker
      </a>
    </li>
    <li>
      <a href="../quick-start/kubernetes/" class="nav-link">
        <i class="fas fa-cubes" aria-hidden="true"></i>
        Kubernetes
      </a>
    </li>
  </ul>
</div>

This is a second-level tab widget, that uses different files for each tab - same as the one above, just styled differently. We should be using this style instead of the buttons for this type of tab. Many pages need to be changed to do that.

Everything that follows is in this tab's file. If you change tabs, you get a whole new page.

## Heading 2

The following is a basic markdown table.

| Table | A column |
| :--- | :--- |
| A row | Another column in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |
| Another row | Another row in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |
| Another row | Another column in a table. Maybe to describe stuff. Might have bulleted lists etc, but that all has to be done using HTML. |

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list. We don't use this much, but it might be useful in some contexts.

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list. We don't use this much, but it might be useful in some contexts.
: Another paragraph in the definition.

Glossary term
: Definition. This text is a definition for a glossary term, or any sort of definition list. We don't use this much, but it might be useful in some contexts.
: Another paragraph in the definition.

### Heading 3

An ordinary paragraph.

>**Blockquote**
>
>For content that is outside of a set of instructions; sort of explanatory but peripheral to the task at hand. Advice, what's happening here sort of thing. But not a full blown Note or Warning. It's actually a blockquote in markdown. I'm the only one who uses it. Sorry.
>
>The "heading" is just bold text.

{{< note title="Note" >}}

This is an actual Note. Maybe a bit overkill.

{{< /note >}}

{{< tip title="Tip" >}}

This is an actual Tip. Maybe there's a cool alternative to what you are doing.

{{< /tip >}}

{{< warning title="Warning" >}}

This is an actual Warning. Whatever you are doing may threaten your data if you do it wrong.

{{< /warning >}}

What follows is an image, some numbered steps, some code, text indented under a step:

![Connect using cloud shell](/images/yb-cloud/cloud-connect-shell.gif)

1. Numbered list with indented lists.

    1. Second level numbered list indent.

    1. Second level numbered list indent.

        1. Third level numbered list indent.

        1. Third level numbered list indent.

1. Under **Cloud Shell**, click **Launch Cloud Shell**.

1. Enter the database name (`yugabyte`), the user name (`admin`), select the YSQL API type, and click **Confirm**.

    Cloud Shell opens in a separate browser window. Cloud Shell can take up to 30 seconds to be ready.

    ```output
    Enter your DB password:
    ```

1. Enter the password for the admin user credentials that you saved when you created the cluster.

    The shell prompt appears and is ready to use.

    ```output
    ysqlsh (11.2-YB-2.2.0.0-b0)
    SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
    Type "help" for help.

    yugabyte=>
    ```

Bulleted lists with levels of indent:

- Bulleted list with indented lists.

  - Second level Bulleted list indent.

  - Second level Bulleted list indent.

    - Third level Bulleted list indent.

    - Third level Bulleted list indent.

#### Heading 4

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). The tutorials in this section show how to connect applications to YugabyteDB Managed clusters using your favorite programming language.

Some bullets:

- a cluster deployed in YugabyteDB Managed.
- the cluster CA certificate; YugabyteDB Managed uses TLS to secure connections to the database.
- your computer added to the cluster IP allow list.

Refer to [Before you begin](../develop/build-apps/cloud-add-ip/).

#### Details tag

The details HTML tag is used to create an interactive widget that the user can open and close. By default, the widget is closed. When open, it expands, and displays the contents.

<details>
  <summary>Tiresomely long code block</summary>

These contents might be a very long bit of code or somesuch.

</details>

### Another tab widget style - in-page

This tab widget doesn't use separate files to fill in the content and then link between. Here the content is placed inside shortcode.

{{< tabpane code=false >}}

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

    - Open the `app.properties` file located in the application `src/main/resources/` folder.

    - Set the following configuration parameters:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the JDBC driver (the default YugabyteDB YSQL port is 5433).

    - Save the file.

1. Start the application.

    ```sh
    $ java -cp target/yugabyte-simple-java-app-1.0-SNAPSHOT.jar SampleApp
    ```

If you are running the application on a free or single node cluster, the driver displays a warning that the load balance failed and will fall back to a regular connection.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created DemoAccount table.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
```

You have successfully executed a basic Java application that works with YugabyteDB Managed.

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

The following tutorial shows a small [Go application](https://github.com/yugabyte/yugabyte-simple-go-app) that connects to a YugabyteDB cluster using the [Go PostgreSQL driver](../drivers-orms/go/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB Managed in Go.

### Headings inside this widget don't show up in RightNav

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-go-app.git && cd yugabyte-simple-go-app
    ```

1. Provide connection parameters.

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    1. Open the `sample-app.go` file.

    2. Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. To obtain a YugabyteDB Managed cluster host name, sign in to YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number that will be used by the driver (the default YugabyteDB YSQL port is 5433).
        - **dbName** - the name of the database you are connecting to (the default database is named `yugabyte`).

    3. Save the file.

1. Start the application.

    ```sh
    $ go run sample-app.go
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Go application that works with YugabyteDB Managed.

[Explore the application logic](../develop/build-apps/go/cloud-ysql-go/#explore-the-application-logic)

  {{% /tab %}}

{{< /tabpane >}}

## Syntax documentation

```sh
ysqlsh [ <option>...] [ <dbname> [ <username> ]]
```

### Default flags

When you open `ysqlsh`, the following default flags (aka flags) are set so that the user doesn't have to specify them.

- host: `-h 127.0.0.1`
- port: `-p 5433`
- user: `-U yugabyte`

---

## Horizontal rule

Once in awhile, there is a horizontal rule. No, really.

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
