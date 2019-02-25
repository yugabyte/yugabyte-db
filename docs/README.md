# YugaByte DB Docs

This repository contains the documentation for YugaByte DB available at https://docs.yugabyte.com/

Please [open an issue](https://github.com/YugaByte/docs/issues) to suggest enhancements.


# Contributing to YugaByte DB Docs

YugaByte DB docs are based on the Hugo framework and use the Material Docs theme.

* Hugo framework: http://gohugo.io/overview/introduction/
* Material Docs theme: http://themes.gohugo.io/material-docs/


## Step 1. Initial setup

Follow these steps if this is the first time you are setting up the YugaByte docs repo locally.

1. Fork this repository on GitHub and create a local clone of your fork. This should look something like below:
```
git clone git@github.com:<YOUR_GITHUB_ID>/docs.git
```

2. Install Hugo. For example, on a Mac, you can run the following commands:
```
brew update
brew install hugo
brew install npm
```

3. Copy the config.yaml.sample to config.yaml.
```
cp config.yaml.sample config.yaml
```

4. Install node modules as shown below:
```
$ npm install
```

## Step 2. Update your docs repo and start the local webserver

1. Add the master as a remote branch by running the following:
```
$ git remote add --track master upstream https://github.com/YugaByte/docs.git
```

2. Fetch the latest docs:
```
$ git fetch upstream
$ git merge upstream/master
```

3. Start the local webserver on `127.0.0.1` interface by running the following:
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

Make the changes locally and test them on the browser.

Once you are satisfied with your changes, commit them to your local branch. You can do this by running the following command:
```
# Add all files you have made changes to.
$ git add -A

# Commit these changes.
$ git commit
```

## Step 4. Submit a pull request

Create a [pull request in the YugaByte docs repo](https://github.com/YugaByte/docs/pulls) once you are ready to submit your changes.

We will review your changes, add any feedback and once everything looks good merge your changes into the mainline.
