# [Apache AGE (incubating)](https://age.apache.org/#)

[![license badge](https://img.shields.io/badge/apache-license--v2.0-brightgreen)](https://github.com/apache/incubator-age/releases)
[![release badge](https://img.shields.io/badge/release-v0.3.0-brightgreen)](https://github.com/apache/incubator-age/releases)
[![issue badge](https://img.shields.io/github/issues/apache/incubator-age)](https://github.com/apache/incubator-age/issues)
[![forks badge](https://img.shields.io/github/forks/apache/incubator-age)](https://github.com/apache/incubator-age/network/members)
[![stars badge](https://img.shields.io/github/stars/apache/incubator-age)](https://github.com/apache/incubator-age/stargazers)

<img src="https://age.apache.org/docs/_static/age_BI.png" width="30%" height="30%">

Apache AGE is a PostgreSQL Extension that provides graph database functionality. AGE is an acronym for A Graph Extension, and is inspired by Bitnine's fork of PostgreSQL 10, AgensGraph, which is a multi-model database. The goal of the project is to create single storage that can handle both relational and graph model data so that users can use standard ANSI SQL along with openCypher, the Graph query language.

## Features

- **Apache AGE is currently being developed for the PostgreSQL 11 release** and will support PostgreSQL 12 and 13 in 2021 and all the future releases of PostgreSQL.
- Apache AGE supports the openCypher graph query language and label hierarchy.
- Apache AGE enables querying multiple graphs at the same time. This will allow a user to query two or more graphs at once with cypher, decide how to merge them and get the desired query outputs.
- Apache AGE will be enhanced with an aim to support all of the key features of AgensGraph (PostgreSQL fork extended with graph DB functionality).

## Installation

- [Use a docker image - official ver.](https://hub.docker.com/r/sorrell/agensgraph-extension)
- [Use a docker image - alpine ver.](https://hub.docker.com/r/sorrell/agensgraph-extension-alpine)
- [Installing from source](https://age.apache.org/#)

## Documentation

You can find Apache AGE documentation [on the website](https://age.apache.org/docs/).

The documentation is divided into several sections:

- [Cypher Query Language](https://age.apache.org/docs/cypher-query-language.html)
- [Utility Functions](https://age.apache.org/docs/utility-functions.html)
- [Tables](https://age.apache.org/docs/tables.html)
- [Installation](https://age.apache.org/docs/installation.html)

## Contribution

You can improve it by sending pull requests to [this repository](https://github.com/apache/incubator-age).  
Also, you can learn from the code review process, how to merge pull requests, and from code style compliance to documentation, by visiting the [Apache AGE official site - Developer Guidelines](https://age.apache.org/#codereview).
