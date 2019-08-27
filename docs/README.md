# YugaByte DB Docs

This repository contains the documentation for YugaByte DB available at https://docs.yugabyte.com/

Please [open an issue](https://github.com/YugaByte/docs/issues) to suggest enhancements.


# Contributing to YugaByte DB Docs

YugaByte DB docs are based on the Hugo framework and use the Material Docs theme.

* Hugo framework: http://gohugo.io/overview/introduction/
* Material Docs theme: http://themes.gohugo.io/material-docs/


## Step 1. Initial setup

Follow these steps if this is the first time you are setting up for working on the docs locally.

1. Fork this repository on GitHub and create a local clone of your fork. This should look something like below:
   ```sh
   git clone git@github.com:<YOUR_GITHUB_ID>/yugabyte-db.git
   ```

   Add the original repository as an upstream remote:
   ```sh
   git remote add --track master upstream https://github.com/YugaByte/yugabyte-db.git
   ```

1. Install Hugo. For example, on a Mac, you can run the following commands:
   ```sh
   brew update
   brew install hugo
   brew install npm
   ```

1. Install node modules as shown below:
   ```sh
   npm ci
   ```

## Step 2. Update your repo and start the local webserver

The assumption here is that you are working on a local clone of your fork. See the previous step.

1. Rebase your fork to fetch the latest docs changes:
Ensure you are on the master branch.
```
$ git checkout master
```

Now rebase to the latest changes.
```
$ git pull --rebase upstream master
$ git push origin master
```

2. Start the local webserver on `127.0.0.1` interface by running the following:
```
$ npm start
```

You should be able to see the local version of the docs by browsing to:
http://localhost:1313/

**Note #1** that the URL may be different if the port 1313 is not available. In any case, the URL is printed out on your shell as shown below.
```
Web Server is available at //localhost:1313/ (bind address 0.0.0.0)
Press Ctrl+C to stop
```

**Note #2** To start the webserver on some other IP address (in case you want to share the URL of your local docs with someone else), do the following:
```
YB_HUGO_BASE=<YOUR_IP_OR_HOSTNAME> npm start
```
You can now share the following link: `http://<YOUR_IP_OR_HOSTNAME>:1313`


## Step 3. Make changes

It is suggested that you create and checkout a feature branch off `master` for your changes:
```
$ git branch <feature branch name> master
$ git checkout <feature branch name>
```

Make the changes locally and test them on the browser.

Once you are satisfied with your changes, commit them to your local branch. You can do this by running the following command:
```
# Add files you have made changes to.
$ git add ...

# Commit these changes.
$ git commit
```

## Step 4. Submit a pull request

Create a pull request once you are ready to submit your changes.

We will review your changes, add any feedback and once everything looks good merge your changes into the mainline.


## Advanced Usage

### Generate API syntax diagrams

To update the documentation with new grammars and syntax diagrams, follow these steps:

_Note: Modifications will typically be added to the `ysql` or `ycql` subdirectories of the `docs/content/latest/api/` directory._

1. Update the appropriate EBNF source grammar file (`<source>_grammar.ebnf`) with your changes, for example, adding support for a new statement or clause.

    - YSQL API: `./content/latest/api/ysql/syntax_resources/ysql_grammar.ebnf`
    - YCQL API: `./content/latest/api/ycql/syntax_resources/ycql_grammar.ebnf`

2. If you are adding a new file (for example for a new statement), use the template `includeMarkdown` macro (see `cmd_copy.md` file mentioned below as an example).

    _Example: for the YSQL `COPY` command the source file is `./content/latest/api/ysql/commands/cmd_copy.md`._

3. Inside of the `syntax_resources/commands` directory, create the following two **empty** files:
    - `<name>.grammar.md`
    - `<name>.diagram.md`

    For `<name>`, use a comma-separated list of rule names from the EBNF file that you want in your new file.
    The two new files must be added into a directory structure that matches the top-level directory (`ysql` or `ycql`) — if needed, create any required parent directories.

    _Example: For the commands/cmd_copy.md case, the new files would be named as follows:_
    ```
    ./content/latest/api/ysql/syntax_resources/commands/copy_from,copy_to,copy_option.grammar.md
    ./content/latest/api/ycql/syntax_resources/commands/copy_from,copy_to,copy_option.diagram.md
    ```

4. Download the latest RRDiagram JAR file (`rrdiagram.jar`).  You can find it on the [release
   page](https://github.com/YugaByte/RRDiagram/releases/), or you can try running the following
   command.

   ```bash
   wget $(curl -s https://api.github.com/repos/YugaByte/RRDiagram/releases/latest \
          | grep browser_download_url | cut -d \" -f 4)
   ```

   _Note: Alternatively, you can manually build the jar file as described in the [build
   section](https://github.com/YugaByte/RRDiagram/README.md#build) of the `RRDiagram` repo (and
   move/rename the resulting jar from the target folder)._

5. Run the diagram generator using the following command:

    ```bash
    java -jar rrdiagram.jar <input-file.ebnf> <output-folder>
    ```

    `<output-folder>` should end in `syntax_resources`, not `commands`, to get helpful `WARNING`s.

    _Example: To generate the syntax diagrams for the YSQL API, run the following command:_
    ```bash
    java -jar rrdiagram.jar content/latest/api/ysql/syntax_resources/ysql_grammar.ebnf content/latest/api/ysql/syntax_resources/
    ```

    All of the Markdown (`.md`) files in the `./ysql/syntax_resources/commands` directory will be generated as needed.

    _Note: To see help, run `java -jar rrdiagram.jar` (without arguments)._

6. Check that the output file looks fine.
    You may need to save the main Markdown (`.md`) file (for example `commands/cmd_copy.md`) to force the page to be rerendered.
