// Copyright (c) YugaByte, Inc.

# README #

YugaByte Product Documentation

### What is this repository for? ###


Our docs are based on the Hugo framework and are using the Material Docs theme

** Hugo framework: http://gohugo.io/overview/introduction/

** Material Docs theme: http://themes.gohugo.io/material-docs/

** Example of Material docs theme: http://themes.gohugo.io/theme/material-docs/


### How do I get set up? ###

## Install Hugo ##
```
brew update
brew install hugo
brew install npm
```

## Prerequisites ##
Copy the config.yaml.sample to config.yaml and add any Third-party credentials needed.
```
cp config.yaml.sample config.yaml
```


## To run locally ##
```
npm start
```

If you would like to share the URL of your local docs with someone else, you can do that through:
```
YB_HUGO_BASE=<YOUR_IP_OR_HOSTNAME> npm start
```

Then, just share the `<YOUR_IP_OR_HOSTNAME>:1313` link to whomever you want to view your docs changes.

## To publish to S3 ##
Caution: This would publish the docs to our website which is used by customers
```
npm run publish
```

## Update algolia index ##
```
export ALGOLIA_WRITE_KEY=<Admin Key>
npm run publish-index
```
The last command above starts high performance web server that continuously watches the entire docs directory. Go to [Home page](/)to see the docs.

Simply running "hugo" will create a directory called public that has all the html content necessary to deploy to production behind an existing web server.

Detailed installation instructions: http://gohugo.io/overview/installing/
Detailed usage instructions: http://gohugo.io/overview/usage/
