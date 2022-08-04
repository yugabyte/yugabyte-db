---
title: JanusGraph
linkTitle: JanusGraph
description: JanusGraph
aliases:
menu:
  preview:
    identifier: janusgraph
    parent: integrations
    weight: 571
type: docs
---

In this tutorial, you first set up [JanusGraph](https://janusgraph.org/) to work with YugabyteDB as the underlying database. Then, using the Gremlin console, you load some data and run some graph commands.

## 1. Start local cluster

Start a cluster on your [local computer](../../quick-start/). Check that you are able to connect to YugabyteDB using `ycqlsh` by executing the following:

```sh
$ ycqlsh
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
```

```sql
ycqlsh> DESCRIBE KEYSPACES;
```

```output
system_schema  system_auth  system

ycqlsh>
```

## 2. Download JanusGraph

- Download from the [JanusGraph downloads page](https://github.com/JanusGraph/janusgraph/releases). This tutorial uses the `0.2.0` version of JanusGraph.

```sh
$ wget https://github.com/JanusGraph/janusgraph/releases/download/v0.2.0/janusgraph-0.2.0-hadoop2.zip
$ unzip janusgraph-0.2.0-hadoop2.zip
$ cd janusgraph-0.2.0-hadoop2/lib
```

- Download the [cassandra-driver-core-3.8.0-yb-6.jar](https://repo1.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.8.0-yb-6/cassandra-driver-core-3.8.0-yb-6.jar) and copy it into `janusgraph-0.2.0-hadoop2/lib`.

- Rename the existing cassandra driver.

```sh
mv cassandra-driver-core-3.1.4.jar cassandra-driver-core-3.1.4.jar.orig
```

## 3. Run JanusGraph with YugabyteDB

- Start the Gremlin console by running `./bin/gremlin.sh`. You should see something like the following.

```sh
$ ./bin/gremlin.sh
```

```output
         \,,,/
         (o o)
-----oOOo-(3)-oOOo-----
plugin activated: janusgraph.imports
plugin activated: tinkerpop.server
plugin activated: tinkerpop.utilities
plugin activated: tinkerpop.hadoop
plugin activated: tinkerpop.spark
plugin activated: tinkerpop.tinkergraph
gremlin>
```

- Now use the YCQL config to initialize JanusGraph to talk to Yugabyte.

```sql
gremlin> graph = JanusGraphFactory.open('conf/janusgraph-cql.properties')
```

```output
==>standardjanusgraph[cql:[127.0.0.1]]
```

- Open the YugabyteDB UI to verify that the `janusgraph` keyspace and the necessary tables were created by opening the following URL in a web browser: `http://localhost:7000/` (replace `localhost` with the IP address of any master node in a remote deployment). You should see the following.

![List of keyspaces and tables when running JanusGraph on YugabyteDB](/images/develop/ecosystem-integrations/janusgraph/yb-janusgraph-tables.png)

## 4. Load sample data

We are going to load the sample data that JanusGraph ships with - the Graph of the Gods. You can do this by running the following:

```sh
gremlin> GraphOfTheGodsFactory.loadWithoutMixedIndex(graph,true)
```

```output
==>null
```

```sql
gremlin> g = graph.traversal()
```

```output
==>graphtraversalsource[standardjanusgraph[cql:[127.0.0.1]], standard]
```

## 5. Graph traversal examples

For reference, here is the graph data loaded by the Graph of the Gods. You can find a lot more useful information about this in the [JanusGraph getting started page](https://docs.janusgraph.org/getting-started/basic-usage/).

![Graph of the Gods](/images/develop/ecosystem-integrations/janusgraph/graph-of-the-gods-2.png)

- Retrieve the Saturn vertex

```sh
gremlin> saturn = g.V().has('name', 'saturn').next()
```

```output
==>v[4168]
```

- Who is Saturn’s grandchild?

```sh
gremlin> g.V(saturn).in('father').in('father').values('name')
```

```output
==>hercules
```

- Queries about Hercules

```sh
gremlin> hercules = g.V(saturn).repeat(__.in('father')).times(2).next()
```

```output
==>v[4120]
```

```sql
// Who were the parents of Hercules?
gremlin> g.V(hercules).out('father', 'mother').values('name')
```

```output
==>jupiter
==>alcmene
```

```sql
// Were the parents of Hercules gods or humans?
gremlin> g.V(hercules).out('father', 'mother').label()
```

```output
==>god
==>human
```

```sql
// Who did Hercules battle?
gremlin> g.V(hercules).out('battled').valueMap()
```

```output
==>[name:[hydra]]
==>[name:[nemean]]
==>[name:[cerberus]]
```

```sql
// Who did Hercules battle after time 1?
gremlin> g.V(hercules).outE('battled').has('time', gt(1)).inV().values('name')
```

```output
==>cerberus
==>hydra
```

## 6. Complex graph traversal examples

- Who are Pluto's cohabitants?

```sql
gremlin> pluto = g.V().has('name', 'pluto').next()
```

```output
==>v[8416]
```

```sql
// who are pluto's cohabitants?
gremlin> g.V(pluto).out('lives').in('lives').values('name')
```

```output
==>pluto
==>cerberus
```

```sql
// pluto can't be his own cohabitant
gremlin> g.V(pluto).out('lives').in('lives').where(is(neq(pluto))).values('name')
```

```output
==>cerberus
```

```sql
gremlin> g.V(pluto).as('x').out('lives').in('lives').where(neq('x')).values('name')
```

```output
==>cerberus
```

- Queries about Pluto’s Brothers.

```sql
// where do pluto's brothers live?
gremlin> g.V(pluto).out('brother').out('lives').values('name')
```

```output
==>sea
==>sky
```

```sql
// which brother lives in which place?
gremlin> g.V(pluto).out('brother').as('god').out('lives').as('place').select('god', 'place')
```

```output
==>[god:v[4248],place:v[4320]]
==>[god:v[8240],place:v[4144]]
```

```sql
// what is the name of the brother and the name of the place?
gremlin> g.V(pluto).out('brother').as('god').out('lives').as('place').select('god', 'place').by('name')
```

```output
==>[god:neptune,place:sea]
==>[god:jupiter,place:sky]
```

## 7. Global graph index examples

Geo-spatial indexes - events that have happened within 50 kilometers of Athens (latitude:37.97 and long:23.72).

```sql
// Show all events that happened within 50 kilometers of Athens (latitude:37.97 and long:23.72).
gremlin> g.E().has('place', geoWithin(Geoshape.circle(37.97, 23.72, 50)))
```

```output
==>e[4cj-36g-7x1-6c8][4120-battled->8216]
==>e[3yb-36g-7x1-9io][4120-battled->12336]
```

```sql
// For these events that happened within 50 kilometers of Athens, show who battled whom.
gremlin> g.E().has('place', geoWithin(Geoshape.circle(37.97, 23.72, 50))).as('source').inV().as('god2').select('source').outV().as('god1').select('god1', 'god2').by('name')
```

```output
==>[god1:hercules,god2:hydra]
==>[god1:hercules,god2:nemean]
```
