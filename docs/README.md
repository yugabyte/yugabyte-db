# YugabyteDB Docs

This repository contains the source code for the [public documentation for YugabyteDB](https://docs.yugabyte.com/). Please [open an issue](https://github.com/YugaByte/docs/issues) to request features or suggest enhancements.


# Contributing to YugabyteDB Docs

## Prerequisites
YugabyteDB docs are based on the Hugo framework and use the Material Docs theme.

* Hugo framework: http://gohugo.io/overview/introduction/
* Material Docs theme: http://themes.gohugo.io/material-docs/
* Ensure that you have cloned the yugabyte repository

```
git clone https://github.com/yugabyte/yugabyte-db.git
cd ./yugabyte-db/docs
```

## [Option 1] Docker Setup
For those wanting to contribute, without making direct modifications to system packages, you can download the yugabyte-db docs dockerfile for easy, simple setup for contributing to Yugabyte docs.

1. Build the contianer with the provided file
```
docker build -t yb-docs  .
```
2. Once built, run the container
```
docker run -d --rm -v `pwd`/..:/yugabyte-db/ -p 1313:1313 --name yb-docs yb-docs
```
3. NOTE: The build will take some time, to monitor the build follow the logs.
```
docker logs -f yb-docs
```

## [Option 2] Local setup
To run hugo locally

1. Install Hugo. For example, on a Mac, you can run the following commands:
```
brew update
brew install hugo
brew install npm
```

2. From the docs directory, install node modules:
```
$ npm ci
```

3. Start the local webserver:
```
$ npm start
```

## Viewing local docs
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


## Next Steps: Identify an issue

There needs to be a GitHub issue describing the enhancement you intend to do. Please post a comment on the issue you plan to work on before starting to work on it. This is to ensure someone else does not end up working on this in parallel.

In case you do not have an issue and you're looking for one to work on, here is some useful information to help you:
* All issues are tracked using [GitHub issues](https://github.com/yugabyte/yugabyte-db/issues)
* Documentation related issues have the `area/documentation` label, here is a [list of documentation issues](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation)
* If you are new, look for [issues with the label `good first issue`](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation+label%3A%22good+first+issue%22). If none exist, join [the community slack](https://www.yugabyte.com/slack) and let us know in the `contributors` channel.
* You can also look for [issues with the label `help wanted`](https://github.com/yugabyte/yugabyte-db/issues?q=is%3Aopen+is%3Aissue+label%3Aarea%2Fdocumentation+label%3A%22help+wanted%22+). If none exist, join [the community slack](https://www.yugabyte.com/slack) and let us know in the `contributors` channel.

If you run into any issues, please let us know by posting in our [the community slack](https://www.yugabyte.com/slack), we really appreciate your feedback (and love to talk to you as well).

## Next Steps: Contributing

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

### Submit a pull request

Create a [pull request in the YugabyteDB docs repo](https://github.com/yugabyte/docs/pulls) once you are ready to submit your changes.

We will review your changes, add any feedback and once everything looks good merge your changes into the mainline.
