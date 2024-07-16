<br>

<p align="center">
     <img src="https://age.apache.org/age-manual/master/_static/logo.png" width="30%" height="30%">
</p>
<br>

<h3 align="center">
    <a href="https://age.apache.org/age-manual/master/_static/logo.png" target="_blank">
        <img src="https://age.apache.org/age-manual/master/_static/logo.png" height="25" height="30% alt="Apache AGE style="margin: 0 0 -3px 0">
    </a>
    <a href="https://age.apache.org/age-manual/master/_static/logo.png" target="_blank">
    </a>
     is a leading multi-model graph database </h3>
     
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
    <img src="https://img.shields.io/badge/Release-v1.5.0-FFA500?labelColor=gray&style=flat&link=https://github.com/apache/age/releases"/>
  </a>
  &nbsp;
  <a href="https://www.postgresql.org/docs/15/index.html">
    <img src="https://img.shields.io/badge/Version-Postgresql 15-00008B?labelColor=gray&style=flat&link=https://www.postgresql.org/docs/15/index.html"/>
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
  <a href="https://discord.gg/EuK6EEg3k7">
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

Since AGE is based on the powerful PostgreSQL RDBMS, it is robust and fully featured. AGE is optimized for handling complex connected graph data. It provides plenty of robust database features essential to the database environment, including ACID transactions, multi-version concurrency control (MVCC), stored procedure, triggers, constraints, sophisticated monitoring, and a flexible data model (JSON). Users with a relational database background who require graph data analytics can use this extension with minimal effort because they can use existing data without going through migration. 

There is a strong need for cohesive, easy-to-implement multi-model databases. As an extension of PostgreSQL, AGE supports all the functionalities and features of PostgreSQL while also offering a graph model to boot.


<h2><img height="30" src="/img/tick.svg">&nbsp;&nbsp;Overview</h2>

Apache AGE is :

- **Powerful**: adds graph database support to the already popular PostgreSQL database: PostgreSQL is used by organizations including Apple, Spotify, and NASA.
- **Flexible**: allows you to perform openCypher queries, which makes complex queries much easier to write. It also enables querying multiple graphs at the same time.
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



<h2><img height="30" src="/img/documentation.svg">&nbsp;&nbsp;Documentation</h2>

Refer to our latest [Apache AGE documentation](https://age.apache.org/age-manual/master/index.html) to learn about installation, features, built-in functions, and  Cypher queries.



<h2><img height="30" src="/img/installation.svg">&nbsp;&nbsp;Pre-Installation</h2>

Install the following essential libraries according to each OS. Building AGE from the source depends on the following Linux libraries (Ubuntu package names shown below):

- **CentOS**
```bash
yum install gcc glibc glib-common readline readline-devel zlib zlib-devel flex bison
```
- **Fedora**
```bash
dnf install gcc glibc bison flex readline readline-devel zlib zlib-devel
```
- **Ubuntu**
```bash
sudo apt-get install build-essential libreadline-dev zlib1g-dev flex bison
```

<h2><img height="30" src="/img/installation.svg">&nbsp;&nbsp;Installation</h2>

Apache AGE is intended to be simple to install and run. It can be installed with Docker and other traditional ways. 

<h4><a><img width="20" src="/img/pg.svg"></a>
&nbsp;Install PostgreSQL
</h4>

You will need to install an AGE compatible version of Postgres<a>, for now AGE supports Postgres 11, 12, 13, 14 & 15. Supporting the latest versions is on AGE roadmap.

<h4>
&nbsp;Installation via Package Manager
</h4>

You can use a <a href="https://www.postgresql.org/download/">package management </a> that your OS provides to download PostgreSQL.

<br>

```bash
sudo apt install postgresql

```
<h4>
&nbsp;Installation From Source Code
</h4>

You can <a href="https://www.postgresql.org/ftp/source/"> download the Postgres </a> source code and install your own instance of Postgres. You can read instructions on how to install from source code for different versions on the <a href="https://www.postgresql.org/docs/15/installation.html">official Postgres Website.</a>



<h4><img width="20" src="/img/tux.svg"><img width="20" src="/img/apple.svg"> &nbsp;Install AGE on Linux and MacOS
</h4>

Clone the <a href="https://github.com/apache/age">github repository</a> or download the <a href="https://github.com/apache/age/releases">download an official release.
</a>
Run the pg_config utility and check the version of PostgreSQL. Currently, only PostgreSQL versions 11, 12, 13, 14 & 15 are supported. If you have any other version of Postgres, you will need to install PostgreSQL version 11, 12, 13, 14, or 15.
<br>
    
```bash
pg_config
```
Run the following command in the source code directory of Apache AGE to build and install the extension.  
     
```bash
make install
```
     
If the path to your Postgres installation is not in the PATH variable, add the path in the arguments:
```bash
make PG_CONFIG=/path/to/postgres/bin/pg_config install
```


<h4></a><img width="30" src="/img/docker.svg"></a>
&nbsp;Run using Docker
</h4>

<h5> Get the docker image </h5>

```bash
docker pull apache/age

```
<h5> Create AGE docker container </h5>

```bash
docker run \
    --name age  \
    -p 5455:5432 \
    -e POSTGRES_USER=postgresUser \
    -e POSTGRES_PASSWORD=postgresPW \
    -e POSTGRES_DB=postgresDB \
    -d \
    apache/age
```

<h5> Enter PostgreSQL's psql: </h5>

```bash
docker exec -it age psql -d postgresDB -U postgresUser
```



<h2><img height="20" src="/img/contents.svg">&nbsp;&nbsp;Post Installation</h2>

For every connection of AGE you start, you will need to load the AGE extension.

```bash
CREATE EXTENSION age;
```
```bash
LOAD 'age';
```
```bash
SET search_path = ag_catalog, "$user", public;
```



<h2><img height="20" src="/img/contents.svg">&nbsp;&nbsp;Quick Start</h2>

To create a graph, use the create_graph function located in the ag_catalog namespace.

```bash
SELECT create_graph('graph_name');
```

To create a single vertex, use the CREATE clause. 

```bash
SELECT * 
FROM cypher('graph_name', $$
    CREATE (n)
$$) as (v agtype);
```


To create a single vertex with the label, use the CREATE clause. 

```bash
SELECT * 
FROM cypher('graph_name', $$
    CREATE (:label)
$$) as (v agtype);
```

To create a single vertex with label and properties, use the CREATE clause.

```bash
SELECT * 
FROM cypher('graph_name', $$
    CREATE (:label {property:value})
$$) as (v agtype);
```

To query the graph, you can use the MATCH clause.  

```bash
SELECT * 
FROM cypher('graph_name', $$
    MATCH (v)
    RETURN v
$$) as (v agtype);
```

You can use the following to create an edge, for example, between two nodes. 

```bash
SELECT * 
FROM cypher('graph_name', $$
    MATCH (a:label), (b:label)
    WHERE a.property = 'Node A' AND b.property = 'Node B'
    CREATE (a)-[e:RELTYPE]->(b)
    RETURN e
$$) as (e agtype);
```


To create an edge and set properties.

```bash
SELECT * 
FROM cypher('graph_name', $$
    MATCH (a:label), (b:label)
    WHERE a.property = 'Node A' AND b.property = 'Node B'
    CREATE (a)-[e:RELTYPE {property:a.property + '<->' + b.property}]->(b)
    RETURN e
$$) as (e agtype);
```

Example 

```bash
SELECT * 
FROM cypher('graph_name', $$
    MATCH (a:Person), (b:Person)
    WHERE a.name = 'Node A' AND b.name = 'Node B'
    CREATE (a)-[e:RELTYPE {name:a.name + '<->' + b.name}]->(b)
    RETURN e
$$) as (e agtype);
```



<h2><img height="20" src="/img/gettingstarted.svg">&nbsp;&nbsp;Language Specific Drivers</h2>

Starting with Apache AGE is very simple. You can easily select your platform and incorporate the relevant SDK into your code.
</br>
</br>

<p align="center">
<img src="/img/age-02.png" width="80%" height="80%">
</p>


<h4>Built-in</h4>

- [Go driver](./drivers/golang)
- [Java driver](./drivers/jdbc)
- [NodeJs driver](./drivers/nodejs)
- [Python driver](./drivers/python)

<h4>Community-driven Driver</h4>

- [Apache AGE Rust Driver](https://github.com/Dzordzu/rust-apache-age.git)
- [Apache AGE .NET Driver](https://github.com/Allison-E/pg-age)

<h2><img height="20" src="/img/contributing.svg">&nbsp;&nbsp;Community</h2>

Join the AGE community for help, questions, discussions, and contributions. 

- Check our [website](https://age.apache.org/)
- Ask your AGE-related questions and answers on [Stack overflow](https://stackoverflow.com/questions/tagged/apache-age)
- Discuss about AGE on [GitHub Discussions](https://github.com/apache/age/discussions)
- Share your feedback on [GitHub Issues](https://github.com/apache/age/issues)
- Follow us on [X](https://twitter.com/apache_age)
- Subscribe to our developer mailing list by sending an email to dev-subscribe@age.apache.org
- Subscribe to our user mailing list by sending an email to users-subscribe@age.apache.org
- Subscribe to our committer mailing list (To become a committer) by sending an email to commits-subscribe@age.apache.org


<h2><img height="20" src="/img/visualization.svg">&nbsp;&nbsp;Graph Visualization Tool for AGE</h2>


Apache AGE Viewer is a user interface for Apache AGE that provides visualization and exploration of data.
This web visualization tool allows users to enter complex graph queries and explore the results in graph and table forms.
Apache AGE Viewer is enhanced to proceed with extensive graph data and discover insights through various graph algorithms.
Apache AGE Viewer will become a graph data administration and development platform for Apache AGE to support multiple relational databases: <https://github.com/apache/age-viewer>.

**This is a visualization tool.**
After installing AGE Extension, you may use this tool to get access to the visualization features.


![Viewer gdb, and graph](/img/agce.gif)


<h2><img height="20" src="/img/videos.png">&nbsp;&nbsp;Video Links</h2>

You can also get help from these videos. 

- Install on [Windows](https://www.youtube.com/watch?v=ddk8VX8Hm-I&list=PLGp3huJbWNDjgwP7s99Q-9_w1vxpjNHXG)
- Install on [MacOS](https://www.youtube.com/watch?v=0-qMwpDh0CA)



<h2><img height="20" src="/img/community.svg">&nbsp;&nbsp;Contributing</h2>

You can improve ongoing efforts or initiate new ones by sending pull requests to [this repository](https://github.com/apache/age).
Also, you can learn from the code review process, how to merge pull requests, and from code style compliance to documentation by visiting the [Apache AGE official site - Developer Guidelines](https://age.apache.org/contribution/guide).
Send all your comments and inquiries to the user mailing list, users@age.apache.org.
