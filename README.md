<br>

<p align="center">
     <img src="https://age.apache.org/age-manual/master/_static/logo.png" width="30%" height="30%">
</p>
<br>

<h3 align="center">
    <a href="https://age.apache.org/age-manual/master/_static/logo.png" target="_blank">
        <img src="https://age.apache.org/age-manual/master/_static/logo.png"" height="25" height="30% alt="Apache AGE">
    </a>
    <a href="https://age.apache.org/age-manual/master/_static/logo.png" target="_blank">
    </a>
     is the leading multi-model graph database </h3>
     
</h3>

<h3 align="center">Graph Processing & Analytics for Relational Databases</h3>

<br>


</br>



<p align="center">                                                                                                    
  <a href="https://github.com/apache/age/blob/master/LICENSE">
    <img src="https://img.shields.io/github/license/apache/age"/>
  </a>
  &nbsp;
  <a href="https://github.com/apache/age/releases">
    <img src="https://img.shields.io/badge/Release-v1.1.0-FFA500?labelColor=gray&style=flat&link=https://github.com/apache/age/releases"/>
  </a>
  &nbsp;
  <a href="https://github.com/apache/age/issues">
    <img src="https://img.shields.io/github/issues/apache/age"/>
  </a>
  &nbsp;
  <a href="https://github.com/apache/age/network/members">
    <img src="https://img.shields.io/github/forks/apache/age"/>
  </a>
  &nbsp;
  <a href="https://github.com/apache/age/stargazers">
    <img src="https://img.shields.io/github/stars/apache/age"/>
  </a>
  &nbsp;
  <a href="https://discord.gg/NMsBs9X8Ss">
    <img src="https://img.shields.io/discord/1022177873127280680.svg?label=discord&style=flat&color=5a66f6"></a>
</p>

<br>


<h2><img height="30" src="/img/AGE.png">&nbsp;&nbsp;What is Apache AGE?</h2>

[Apache AGE](https://age.apache.org/#) is an extension for PostgreSQL that enables users to leverage a graph database on top of the existing relational databases. AGE is an acronym for A Graph Extension and is inspired by Bitnine's AgensGraph, a multi-model database fork of PostgreSQL. The basic principle of the project is to create a single storage that handles both the relational and graph data model so that the users can use the standard ANSI SQL along with openCypher, one of the most popular graph query languages today. 
</br>
</br>
</br>

<p align="center">
<img src="/img/age-01.png" width="80%" height="80%">
</p>

</br>

Since AGE is based on the powerful PostgreSQL RDBMS, it is robust and fully featured. AGE is optimized for handling complex connected graph data. It provides plenty of robust databases features essential to the database environment, including ACID transactions, multi-version concurrency control (MVCC), stored procedure, triggers, constraints, sophisticated monitoring, and a flexible data model (JSON). Users with a relational background who require graph data analytics can use this extension with minimal effort because they can use existing data without having to go through migration. 

There is a strong need for cohesive, easy-to-implement multi-model databases. As an extension of PostgreSQL, AGE supports all the functionalities and features of PostgreSQL while also offering a graph model to boot.


<h2><img height="30" src="/img/tick.svg">&nbsp;&nbsp;Overview</h2>

Apache AGE is : 

- **Powerful**: adds graph database support to the already popular PostgreSQL database: PostgreSQL is used by organizations including Apple, Spotify, and NASA.
- **Flexible**: allows you to perform openCypher queries, which makes complex queries much easier to write. It also enables multiple graphs at the same time.
- **Intelligent**: allows you to perform graph queries that are the basis for many next-level web services such as fraud detection, master data management, product recommendations, identity and relationship management, experience personalization, knowledge management, and more.

<h2><img height="30" src="/img/features.svg">&nbsp;&nbsp;Features</h2>
</br>
</br>

<p align="center">
<img src="/img/age-03.png" width="80%" height="80%">
</p>
</br>

- **Cypher Query**: supports graph query language
- **Hybrid Querying**: enables SQL and/or Cypher
- **Querying**: enables multiple graphs
- **Hierarchical**: graph label organization
- **Property Indexes**: on both vertices(nodes) and edges
- **Full PostgreSQL**: supports PG features


## Latest Events

- Latest Apache AGE release, [Apache AGE 1.1.0](https://github.com/apache/age/releases/tag/v1.1.0-rc0).
- Renewed Apache AGE homepage - [Apache AGE website](http://age.apache.org/).
- Send all your comments and inquiries to the user mailing list, users@age.apache.org.
- Support for PostgreSQL will be added in the Q4 2022 to focus more on implementing the openCypher specification.

## Documentation

Refer to our latest [Apache AGE documentation](https://age.apache.org/age-manual/master/index.html) to learn about installation, features and built-in functions, and  Cypher queries.

## Installation

- [Installing from source](https://age.apache.org/download)
- [Apache AGE setup guide](https://age.apache.org/age-manual/master/intro/setup.html#)
- [Installing via docker image](https://age.apache.org/age-manual/master/intro/setup.html#installing-via-docker-image)

## Language Specific Drivers

### Built-in

- [Go driver](./drivers/golang)
- [Java driver](./drivers/jdbc)
- [NodeJs driver](./drivers/nodejs)
- [Python driver](./drivers/python)

### Community-driven Driver
- [Apache AGE Rust Driver](https://github.com/Dzordzu/rust-apache-age.git)

## Graph Visualization Tool for AGE

Apache AGE Viewer is a user interface for Apache AGE that provides visualization and exploration of data.
Through this simple web visualization tool, users can enter complex graph queries and explore the results in graph and table forms.
Apache AGE Viewer is enhanced to proceed with large graph data and discover the insights through various graph algorithms.
Apache AGE Viewer will become a graph data administration and development platform for Apache AGE to support multiple relational databases: <https://github.com/apache/age-viewer>.

- This is a visualization tool.
After installing AGE Extension, you may use this tool to get access to the visualization features.

## Contribution

You can improve ongoing efforts or initiate new ones by sending pull requests to [this repository](https://github.com/apache/age).
Also, you can learn from the code review process, how to merge pull requests, and from code style compliance to documentation, by visiting the [Apache AGE official site - Developer Guidelines](https://age.apache.org/contribution/guide).
