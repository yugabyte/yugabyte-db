# YugabyteDB Docs

This repository contains the documentation for YugabyteDB available at https://docs.yugabyte.com/

Please [open an issue](https://github.com/yugabyte/docs/issues) to suggest enhancements.


# Contributing to YugabyteDB Docs

YugabyteDB docs are based on the Hugo framework and use the Material Docs theme.

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
   git remote add --track master upstream https://github.com/yugabyte/yugabyte-db.git
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

_Note: YCQL docs are still using an old method.  Follow these instructions for YSQL, but use these
instructions just as a reference for YCQL._

### Generate API syntax diagrams

1. Download the latest RRDiagram JAR file (`rrdiagram.jar`).  You can find it on the [release
   page](https://github.com/yugabyte/RRDiagram/releases/), or you can try running the following
   command.

   ```sh
   wget $(curl -s https://api.github.com/repos/Yugabyte/RRDiagram/releases/latest \
          | grep browser_download_url | cut -d \" -f 4)
   ```

   _Note: Alternatively, you can manually build the JAR file as described in the [build
   section](https://github.com/yugabyte/RRDiagram#build) of the `RRDiagram` repository._

1. Run the diagram generator using the following command:

   ```sh
   java -jar rrdiagram.jar content/latest/api/ysql/syntax_resources/ysql_grammar.ebnf \
     content/latest/api/ysql/syntax_resources/
   ```

   To display helpful `WARNING` messages, end the last argument with `syntax_resources`, not
   `commands`.

   The following files will be regenerated _if they exist_:

   - `content/latest/api/ysql/syntax_resources/commands/*.diagram.md`
   - `content/latest/api/ysql/syntax_resources/commands/*.grammar.md`
   - `content/latest/api/ysql/syntax_resources/grammar_diagrams.md`

   _Note: To see help, run `java -jar rrdiagram.jar` (without arguments)._

### Add a new docs page

You may need to add a new docs page if you are adding a new command that doesn't belong in the
existing doc pages.

1. Add new rules to the source grammar file:

   - `content/latest/api/ysql/syntax_resources/ysql_grammar.ebnf`

1. Prepare the new diagram and grammar files by creating the following **empty** files:

   - `content/latest/api/ysql/syntax_resources/commands/<rules>.diagram.md`
   - `content/latest/api/ysql/syntax_resources/commands/<rules>.grammar.md`

   For `<rules>`, use a comma-separated list of rule names that you want in your new docs page.

   _Example: for the YSQL 'COPY' command, the diagram and grammar files are the following:_

   - `content/latest/api/ysql/syntax_resources/commands/copy_from,copy_to,copy_option.diagram.md`
   - `content/latest/api/ysql/syntax_resources/commands/copy_from,copy_to,copy_option.grammar.md`

1. Create the docs page file:

   - `content/latest/api/ysql/commands/<name>.md`

   Use the existing files in that directory as examples.  For `<name>`, follow the naming convention
   exhibited by the other files.  For YSQL, note the usage of the template `includeMarkdown` macro.

   _Example: for the YSQL `COPY` command, the created file is the following:_

   - `content/latest/api/ysql/commands/cmd_copy.md`

1. Update the index page files:

   - `content/latest/api/ysql/_index.md`
   - `content/latest/api/ysql/commands/_index.md`

1. [Run the diagram generator](#generate-api-syntax-diagrams).

   Ensure that no `WARNING`s are thrown related to the rules that you added.

1. Check the new docs page and index pages to make sure that there are no broken links.

### Edit rules in a docs page

You may need to add or edit grammar rules in an existing docs page.

1. Add or edit rules in the source grammar file:

   - `content/latest/api/ysql/syntax_resources/ysql_grammar.ebnf`

1. Rename the diagram and grammar files if needed:

   - `content/latest/api/ysql/syntax_resources/commands/<oldrules>.diagram.md` to
     `content/latest/api/ysql/syntax_resources/commands/<newrules>.diagram.md`
   - `content/latest/api/ysql/syntax_resources/commands/<oldrules>.grammar.md` to
     `content/latest/api/ysql/syntax_resources/commands/<newrules>.grammar.md`

1. Edit the contents of the docs page file:

   - `content/latest/api/ysql/commands/<name>.md`

1. [Run the diagram generator](#generate-api-syntax-diagrams).

   Ensure that no `WARNING`s are thrown related to the rules that you added or edited.

1. Check the modified docs page to make sure that there are no broken links.

### Notes

- To force the page to be re-rendered, you may need to save the docs page file (for example,
  `commands/cmd_copy.md`).
