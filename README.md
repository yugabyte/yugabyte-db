# [Apache AGE (incubating)](https://age.apache.org/#)

[![license badge](https://img.shields.io/badge/apache-license--v2.0-brightgreen)](https://github.com/apache/incubator-age/releases)
[![release badge](https://img.shields.io/badge/release-v0.3.0-brightgreen)](https://github.com/apache/incubator-age/releases)
[![issue badge](https://img.shields.io/github/issues/apache/incubator-age)](https://github.com/apache/incubator-age/issues)
[![forks badge](https://img.shields.io/github/forks/apache/incubator-age)](https://github.com/apache/incubator-age/network/members)
[![stars badge](https://img.shields.io/github/stars/apache/incubator-age)](https://github.com/apache/incubator-age/stargazers)

<img src="https://age.apache.org/docs/_static/age_BI.png" width="30%" height="30%">

Apache AGE is a PostgreSQL Extension that provides graph database functionality. AGE is an acronym for A Graph Extension, and is inspired by Bitnine's fork of PostgreSQL 10, AgensGraph, which is a multi-model database. The goal of the project is to create single storage that can handle both relational and graph model data so that users can use standard ANSI SQL along with openCypher, the Graph query language.

## Overview

- **Apache AGE is currently being developed for the PostgreSQL 11 release** and will support PostgreSQL 12 and 13 in 2021 and all the future releases of PostgreSQL.
- Apache AGE supports the openCypher graph query language and label hierarchy.
- Apache AGE enables querying multiple graphs at the same time. This will allow a user to query two or more graphs at once with cypher, decide how to merge them and get the desired query outputs.
- Apache AGE will be enhanced with an aim to support all of the key features of AgensGraph (PostgreSQL fork extended with graph DB functionality).

## Latest happenings 

- The first official Apache release has been released, [Apache AGE 0.3.0 (incubating)](https://github.com/apache/incubator-age/releases/tag/v0.3.0.rc0-incubating).
- The latest Apache AGE documentation is now available at [here](http://age.incubator.apache.org/docs/Apache_AGE_Guide.pdf).
- The roadmap has been updated, please check out the [Apache AGE website](http://age.apache.org/).

## Installation

- [Use a docker image - official ver.](https://hub.docker.com/r/sorrell/agensgraph-extension)
- [Use a docker image - alpine ver.](https://hub.docker.com/r/sorrell/agensgraph-extension-alpine)
- [Installing from source](https://age.apache.org/#)

## Viewer Tools in AGE

### AGViewer : https://github.com/bitnine-oss/AGViewer.git

- This is a visualization tool.
After AGE Extension Installation
You can use this tool to use the visualization features.
- Follow the instructions on the link to run it.
Under Connect to Database , select database type as "Apache AGE"

## Documentation

Here is the link to the latest [Apache AGE documentation](http://age.incubator.apache.org/docs/Apache_AGE_Guide.pdf).
You can learn about how to install Apache AGE, its features and built-in functions and how to use various Cypher queries.  

## Contribution

You can improve ongoing efforts or initiate new ones by sending pull requests to [this repository](https://github.com/apache/incubator-age).  
Also, you can learn from the code review process, how to merge pull requests, and from code style compliance to documentation, by visiting the [Apache AGE official site - Developer Guidelines](https://age.apache.org/#codereview).
