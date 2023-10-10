---
title: JanusGraph
linkTitle: JanusGraph
description: JanusGraph
aliases:
menu:
  preview_integrations:
    identifier: janusgraph
    parent: integrations-other
    weight: 571
type: docs
---

This tutorial describes how to set up [JanusGraph](https://janusgraph.org/) to work with YugabyteDB and use the Gremlin console to load some data and run some graph commands.

## Prerequisites

To use JanusGraph with YugabyteDB, you need the following:

- Install YugabyteDB and start a single node local cluster. Refer to [Quick start](../../quick-start/).
- JanusGraph. You can download from the [JanusGraph downloads page](https://github.com/JanusGraph/janusgraph/releases). This tutorial uses v0.6.2.

  ```sh
  $ wget https://github.com/JanusGraph/janusgraph/releases/download/v0.6.2/janusgraph-0.6.2.zip
  $ unzip janusgraph-0.6.2.zip
  $ cd janusgraph-0.6.2/lib
  ```

- Download [cassandra-driver-core-3.8.0-yb-6.jar](https://repo1.maven.org/maven2/com/yugabyte/cassandra-driver-core/3.8.0-yb-6/cassandra-driver-core-3.8.0-yb-6.jar) and copy it into `janusgraph-0.6.2/lib`.

  Rename the existing Cassandra driver:

  ```sh
  mv cassandra-driver-core-3.1.4.jar cassandra-driver-core-3.1.4.jar.orig
  ```

## Run JanusGraph with YugabyteDB

Start the Gremlin console by running the following:

```sh
$ ./bin/gremlin.sh
```

You should see output similar to the following:

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

Use the YCQL configuration to initialize JanusGraph to talk to YugabyteDB.

```sql
gremlin> graph = JanusGraphFactory.open('conf/janusgraph-cql.properties')
```

```output
==>standardjanusgraph[cql:[127.0.0.1]]
```

Open the YugabyteDB Admin console to verify that the `janusgraph` keyspace and the necessary tables were created by navigating to <http://localhost:7000/> (replace `localhost` with the IP address of any master node in a remote deployment). You should see the following:

![List of keyspaces and tables when running JanusGraph on YugabyteDB](/images/develop/ecosystem-integrations/janusgraph/yb-janusgraph-tables.png)

### Load sample data

You can load the sample data that JanusGraph ships with - the Graph of the Gods. You can do this by running the following:

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

For reference, the following illustration shows the data loaded by the Graph of the Gods. For more information about the dataset, refer to [Basic usage](https://docs.janusgraph.org/getting-started/basic-usage/) in the JanusGraph documentation.

![Graph of the Gods](/images/develop/ecosystem-integrations/janusgraph/graph-of-the-gods-2.png)

## Examples

The following examples are derived from the examples in the JanusGraph documentation.

### Graph traversal

#### Queries about Hercules

Retrieve the Saturn vertex.

```sh
gremlin> saturn = g.V().has('name', 'saturn').next()
```

```output
==>v[4168]
```

Who is Saturn's grandchild?

```sh
gremlin> g.V(saturn).in('father').in('father').values('name')
```

```output
==>hercules
```

Retrieve the Hercules vertex.

```sh
gremlin> hercules = g.V(saturn).repeat(__.in('father')).times(2).next()
```

```output
==>v[4120]
```

Who were the parents of Hercules?

```sql
gremlin> g.V(hercules).out('father', 'mother').values('name')
```

```output
==>jupiter
==>alcmene
```

Were the parents of Hercules gods or humans?

```sql
gremlin> g.V(hercules).out('father', 'mother').label()
```

```output
==>god
==>human
```

Who did Hercules battle?

```sql
gremlin> g.V(hercules).out('battled').valueMap()
```

```output
==>[name:[hydra]]
==>[name:[nemean]]
==>[name:[cerberus]]
```

Who did Hercules battle after time 1?

```sql
gremlin> g.V(hercules).outE('battled').has('time', gt(1)).inV().values('name')
```

```output
==>cerberus
==>hydra
```

### Complex graph traversal

Retrieve the Pluto vertex.

```sql
gremlin> pluto = g.V().has('name', 'pluto').next()
```

```output
==>v[8416]
```

Who are Pluto's cohabitants?

```sql
gremlin> g.V(pluto).out('lives').in('lives').values('name')
```

```output
==>pluto
==>cerberus
```

Pluto can't be his own cohabitant:

```sql
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

#### Queries about Pluto's Brothers

Where do Pluto's brothers live?

```sql
gremlin> g.V(pluto).out('brother').out('lives').values('name')
```

```output
==>sea
==>sky
```

Which brother lives in which place?

```sql
gremlin> g.V(pluto).out('brother').as('god').out('lives').as('place').select('god', 'place')
```

```output
==>[god:v[4248],place:v[4320]]
==>[god:v[8240],place:v[4144]]
```

What is the name of the brother and the name of the place?

```sql
gremlin> g.V(pluto).out('brother').as('god').out('lives').as('place').select('god', 'place').by('name')
```

```output
==>[god:neptune,place:sea]
==>[god:jupiter,place:sky]
```

### Global graph index

Show all events that occurred in 50 kilometers of Athens (latitude:37.97 and long:23.72).

```sql
gremlin> g.E().has('place', geoWithin(Geoshape.circle(37.97, 23.72, 50)))
```

```output
==>e[4cj-36g-7x1-6c8][4120-battled->8216]
==>e[3yb-36g-7x1-9io][4120-battled->12336]
```

For events that occurred in 50 kilometers of Athens, show who battled whom.

```sql
gremlin> g.E().has('place', geoWithin(Geoshape.circle(37.97, 23.72, 50))).as('source').inV().as('god2').select('source').outV().as('god1').select('god1', 'god2').by('name')
```

```output
==>[god1:hercules,god2:hydra]
==>[god1:hercules,god2:nemean]
```
