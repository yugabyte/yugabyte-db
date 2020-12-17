# YugabyteDB Docs

This repository contains the source code for the [public documentation for YugabyteDB](https://docs.yugabyte.com/). Please [open an issue](https://github.com/YugaByte/docs/issues) to request features or suggest enhancements.


# Contributing to YugabyteDB Docs

## Prerequisites
YugabyteDB docs are based on the Hugo framework and use the Material Docs theme.

* Hugo framework: http://gohugo.io/overview/introduction/
* Material Docs theme: http://themes.gohugo.io/material-docs/


## Step 1. Initial setup

Follow these steps if this is the first time you are setting up the YugabyteDB docs repo locally.

1. Fork this repository on GitHub and create a local clone of your fork. This should look something like below:
```
git clone git@github.com:<YOUR_GITHUB_ID>/yugabyte-db.git
cd ./yugabyte-db
```

Add the master as a remote branch by running the following:
```
$ git remote add --track master upstream https://github.com/yugabyte/yugabyte-db.git
```

2. Install Hugo. For example, on a Mac, you can run the following commands:
```
brew update
brew install hugo
brew install npm
```

4. Install node modules as shown below:
```
$ cd docs
$ npm ci
```

## Step 2. Update your docs repo and start the local webserver

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
$ cd docs
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


## Step 3. Identify an issue

There needs to be a GitHub issue describing the enhancement you intend to do. Please post a comment on the issue you plan to work on before starting to work on it. This is to ensure someone else does not end up working on this in parallel.

In case you do not have an issue and you're looking for one to work on, here is some useful information to help you:
* All issues are tracked using [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues)
* Documentation related issues have the `area/documentation` label, here is a [list of documentation issues](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation)
* If you are new, look for [issues with the label `good first issue`](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation+label%3A%22good+first+issue%22). If none exist, join [the community slack](https://www.yugabyte.com/slack) and let us know in the `contributors` channel.
* You can also look for [issues with the label `help wanted`](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation+label%3A%22help+wanted%22+). If none exist, join [the community slack](https://www.yugabyte.com/slack) and let us know in the `contributors` channel.

If you run into any issues, please let us know by posting in our [the community slack](https://www.yugabyte.com/slack), we really appreciate your feedback (and love to talk to you as well).

## Step 4. Make changes

Make the changes locally and test them on the browser. You can make the changes on a branch with
```
$ git checkout -b <short_description_of_branch>
```

Once you are satisfied with your changes, commit them to your local branch. You can do this by running the following command:
```
# Add all files you have made changes to.
$ git add -A

# Commit these changes.
$ git commit
```

## Step 5. Submit a pull request

Create a [pull request in the YugabyteDB docs repo](https://github.com/yugabyte/docs/pulls) once you are ready to submit your changes.

We will review your changes, add any feedback and once everything looks good merge your changes into the mainline.
