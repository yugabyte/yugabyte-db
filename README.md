# [Apache AGE](https://age.apache.org/#)

[![license badge](https://img.shields.io/github/license/apache/age)](https://github.com/apache/age/blob/master/LICENSE)
[![release badge](https://img.shields.io/github/v/release/apache/age?sort=semver)](https://github.com/apache/age/releases)
[![issue badge](https://img.shields.io/github/issues/apache/age)](https://github.com/apache/age/issues)
[![forks badge](https://img.shields.io/github/forks/apache/age)](https://github.com/apache/age/network/members)
[![stars badge](https://img.shields.io/github/stars/apache/age)](https://github.com/apache/age/stargazers)

<img src="https://age.apache.org/docs/master/_static/logo.png" width="30%" height="30%">

Apache AGE is a PostgreSQL Extension that provides graph database functionality. AGE is an acronym for A Graph Extension, and is inspired by Bitnine's fork of PostgreSQL 10, AgensGraph, which is a multi-model database. The goal of the project is to create single storage that can handle both relational and graph model data so that users can use standard ANSI SQL along with openCypher, the Graph query language.

A graph consists of a set of vertices (also called nodes) and edges, where each individual vertex and edge possesses a map of properties. A vertex is the basic object of a graph, that can exist independently of everything else in the graph. An edge creates a directed connection between two vertices. A graph database is simply composed of vertices and edges. This type of database is useful when the meaning is in the relationships between the data. Relational databases can easily handle direct relationships, but indirect relationships are more difficult to deal with in relational databases. A graph database stores relationship information as a first-class entity. Apache AGE gives you the best of both worlds, simultaneously.

Apache AGE is:

Powerful -- AGE adds graph database support to the already popular PostgreSQL database: PostgreSQL is used by organizations including Apple, Spotify, and NASA.
Flexible -- AGE allows you to perform openCypher queries, which make complex queries much easier to write.
Intelligent -- AGE allows you to perform graph queries that are the basis for many next level web services such as fraud & intrusion detection, master data management, product recommendations, identity and relationship management, experience personalization, knowledge management and more.

## Overview

- **Apache AGE is currently being developed for the PostgreSQL 11 release** and will support PostgreSQL 12, 13 and all the future releases of PostgreSQL.
- Apache AGE supports the openCypher graph query language.
- Apache AGE enables querying multiple graphs at the same time.
- Apache AGE will be enhanced with an aim to support all of the key features of AgensGraph (PostgreSQL fork extended with graph DB functionality).

## Latest happenings

- Latest Apache AGE release, [Apache AGE 1.0.0 (https://github.com/apache/age/releases/tag/v1.1.0-rc1).
- The latest Apache AGE documentation is now available at [here](https://age.apache.org/docs/master/index.html).
- The roadmap has been updated, please check out the [Apache AGE website](http://age.apache.org/).
- Send all your comments and inquiries to the user mailing list, users@age.apache.org.
- To focus more on implementing the openCypher specification, the support for PostgreSQL 12 will be added in the Q1 2022. 

## Installation

- [Use a docker image - official ver.](https://hub.docker.com/r/apache/age)
- [Installing from source](https://age.apache.org/#)

## Graph visualization tool for AGE

Apache AGE Viewer is a subproject of the Apache AGE project:  https://github.com/apache/age-viewer

- This is a visualization tool.
After AGE Extension Installation
You can use this tool to use the visualization features.
- Follow the instructions on the link to run it.
Under Connect to Database , select database type as "Apache AGE"

## Documentation

Here is the link to the latest [Apache AGE documentation](https://age.apache.org/docs/master/index.html).
You can learn about how to install Apache AGE, its features and built-in functions and how to use various Cypher queries.

## Language Specific Drivers
[Apache AGE Python Driver](https://github.com/rhizome-ai/apache-age-python)

## Contribution

You can improve ongoing efforts or initiate new ones by sending pull requests to [this repository](https://github.com/apache/age).
Also, you can learn from the code review process, how to merge pull requests, and from code style compliance to documentation, by visiting the [Apache AGE official site - Developer Guidelines](https://age.apache.org/#codereview).
