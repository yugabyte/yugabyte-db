# Smart JDBC driver for YSQL


## Motivation


The current Yugabyte JDBC driver is built on the Postgres JDBC driver. It works fine with YugabyteDB as YugabyteDB is wire compatible with PostgreSQL. However, YugabyteDB being a distributed database can use some smarts to not only increase performance but also obviate the need of external load balancers like HAProxy, pgpool etc. Elaborating on the key motivations.

### Cluster Awareness to eliminate need for a load balancer

Each of the available tservers can be connected to for a database connection. However for convenience client applications just use a single url (host port combination specific to one tserver) and make connections to that. This is not ideal and leads to operational complexities. This is because the load is not uniformly distributed across all the peers. Also in case any tserver crashes or is unavailable due to host specific problems, the client application will stop working despite the presence of other healthy servers in the system. External load balancers like HAProxy and pgpool etc can be used to uniformly distribute the client load to multiple servers behind the proxy, however that has two main disadvantages. Firstly, configure and maintain an extra software component which adds to complexity and two, in a true distributed system like YugabyteDB, clusters are configured to scale up and down depending on the load and in that case the external load balancers also would be needed to everytime made aware of the changing list of servers behind them.

### Topology Awareness to enable geo-partitioned apps

Most of the times client applications would like to connect to subset of available servers for performance reasons. Like latency sensitive applications can choose to just connect to available servers in a particular datacenter/region. It would be really nice if the driver can understand this requirement and directs traffic to only those servers that are placed in the desired region/datacenter.

### Shard Awareness for high performance

All the queries and dmls first go to the server to which they are connected to. As part of the execution the backend determines all the tablets which need to be scanned/written to, by talking to the master ( uses cached information also if present ). It also gets, from the master, the location of those tablets and then opens a scan which remotely fetches data from those locations or sends data to those as the case may be. ( Always the primary tablet location unless follower reads are configured in which case it may go to a secondary copy as well ). These remote fetches or writes adds quite a bit of latency specially for oltp kind of queries and therefore it would be desirable that for each operation the request from client driver hits a server where most likely the data of interest lies locally.

## Design


### Cluster Awareness

An in-built function called ‘yb_servers’ will be added in Yugabyte. The purpose of this function is to return one record of information for each tserver present in the cluster.


<table>
  <tr>
   <td><strong>host</strong>
   </td>
   <td><strong>port</strong>
   </td>
   <td><strong>num_connections</strong>
   </td>
   <td><strong>node_type</strong>
   </td>
   <td><strong>cloud</strong>
   </td>
   <td><strong>region</strong>
   </td>
   <td><strong>zone</strong>
   </td>
   <td><strong>public_ip</strong>
   </td>

  </tr>
  <tr>
   <td>internal ip of the tserver
   </td>
   <td>database port
   </td>
   <td>Number of clients connected (not used now)
   </td>
   <td>current possible values are 'primary' or 'read_replica'
   </td>
   <td>cloud where the server is hosted
   </td>
   <td>region where the server is hosted
   </td>
   <td>zone where the server is hosted
   </td>
   <td>public_ip of the server, may be different from the internal ip
   </td>
  </tr>
</table>

_Connection property:_

A new property is being added: _load-balance_

It expects **true/false** as its possible values. \
In YBClusterAwareDataSource load balancing is true by default. However when using the DriverManager.getConnection() API the 'load-balance' property needs to be set to 'true'.

_How does it work:_

1. The driver transparently fetches the list of all servers when it creates the first connection.
2. After that the driver chooses the least loaded server for subsequent connections. The driver keeps track of the number of connections it has created on each server and hence knows about the least loaded server from it's perspective.
3. Servers list can change with time because of many reasons. Servers can get added/removed from the cluster. Therefore it is essential to refresh the server list frequently. The driver explicitly refreshes this information every time a new connection request comes to it and if the information which it has is more than 5 minutes old.

### Topology Awareness

An additional property _topology-keys_ is added to indicate only servers belonging to the locations indicated by the topology keys would be considered for establishing connections
It expects a _comma_separated_geo_locations_ as it's value(s).

For example: topology-keys=cloud1.region1.zone1,cloud1.region1.zone2

### Shard Awareness

> **NOTE:** This feature is still in the design phase.

### Sharing a single physical connection

> **NOTE:** This feature is still in the design phase.

