# YugaByte DB Docs

This repository contains the documentation for YugaByte DB available at https://docs.yugabyte.com/

Please [open an issue](https://github.com/YugaByte/docs/issues) to suggest enhancements.


# Contributing to YugaByte DB Docs

YugaByte DB docs are based on the Hugo framework and use the Material Docs theme.

* Hugo framework: http://gohugo.io/overview/introduction/
* Material Docs theme: http://themes.gohugo.io/material-docs/


## Setup

1. Fork this repository on GitHub and create a local clone of your fork.

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

4. Start the local webserver on `127.0.0.1` interface by running the following:
```
npm start
```

To start the webserver on some other IP address (in case you want to share the URL of your local docs with someone else), do the following:
```
YB_HUGO_BASE=<YOUR_IP_OR_HOSTNAME> npm start
```

You can now share the following link: `http://<YOUR_IP_OR_HOSTNAME>:1313`

