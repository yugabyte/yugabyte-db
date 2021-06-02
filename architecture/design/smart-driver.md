#Smart JDBC driver for YSQL


##Motivation


The current Yugabyte JDBC driver is built on the Postgres JDBC driver. It works fine with YugabyteDB as YugabyteDB is wire compatible with PostgreSQL. However, YugabyteDB being a distributed database can use some smarts to not only increase performance but also obviate the need of external load balancers like HAProxy, pgpool etc. Elaborating on the four key motivations.

###Load Balancing


Each of the available tservers can be connected to for a database connection. However due to convenience client applications just use a single url (host port combination specific to one tserver) and make connections to that. This is not ideal and leads to operational complexities. This is because the load is not uniformly distributed across all the peers. Also in case any tserver crashes or unavailability due to host specific problems the client application can completely come to a halt, despite the presence of other healthy servers in the system. External load balancers like HAProxy and pgpool etc can be used to uniformly distribute the client load to multiple servers behind the proxy, however that has two main disadvantages. Firstly, configure and maintain an extra software component which adds to complexity and two, in a true distributed system like YugabyteDB, clusters are configured to scale up and down depending on the load and in that case the external load balancers also would be needed to everytime made aware of the changing list of servers behind them.

###Data Aware Query Routing


All the queries and dmls first go to the server to which they are connected to. As part of the execution the backend determines all the tablets which need to be scanned/written to, by talking to the master ( uses cached information also if present ). It also gets, from the master, the location of those tablets and then opens a scan which remotely fetches data from those locations or sends data to those as the case may be. ( Always the primary tablet location unless follower reads are configured in which case it may go to a secondary copy as well ). These remote fetches or writes adds quite a bit of latency specially for oltp kind of queries and therefore it would be desirable that for each operation the request from client driver hits a server where most likely the data of interest lies locally.

###Failure Handling


It may be a possibility that a server becomes unreachable or crashes due to a transient problem or due to problems specific to a particular physical host. As there are more than one servers available, it would be very desirable to transparently handle the failures and do auto-retries. It might not be possible in all the scenarios, without complete replay of transactions and state rebuild, but there are scenarios where this can be extremely useful, like most read operations, some write operations especially on ‘auto-commit=true’ connections.

###Sharing Of One Physical Connection By Multiple Database Connections


Postgres creates a separate backend child process for every connection. This limits the number of possible processes which can be created on a particular machine owing to resources available. This means that the maximum number of clients which can be created on a YugabyteDB cluster of a limited number of servers is also limited. One can argue about adding more and more servers to the cluster if required. This may be a theoretically valid argument but can never be an economically viable option.

The solution to this obviously seems to be to share a single physical connection for multiple logical connections. However this would entail massive changes at the protocol layer and session management layer. The PostgreSQL wire protocol assumes synchronous execution and sharing would mean asynchronous processing which would need modifications at the protocol layer, state maintenance etc.

##Design


###Load Balancing


An in-built function called ‘yb_servers’ will be added in Yugabyte. The purpose of this function is to return one record of information for each tserver present in the cluster.


<table>
  <tr>
   <td><strong>host</strong>
   </td>
   <td><strong>port</strong>
   </td>
   <td><strong>connections</strong>
   </td>
   <td><strong>primary</strong>
   </td>
   <td><strong>cloud</strong>
   </td>
   <td><strong>region</strong>
   </td>
   <td><strong>zone</strong>
   </td>
   <td><strong>publicIp</strong>
   </td>

  </tr>
  <tr>
   <td>internal ip of the tserver
   </td>
   <td>database port
   </td>
   <td>Number of clients connected (not used now)
   </td>
   <td>is a primary server or read replica server
   </td>
   <td>cloud where the server is hosted
   </td>
   <td>region where the server is hosted
   </td>
   <td>zone where the server is hosted
   </td>
   <td>publicIp of the server, may be different from the internal ip
   </td>
  </tr>
</table>

_Connection property:_

A new property is being added: _load-balance_

It expects a string as it’s value. The possible values are



*   **true/false** - For uniform load balancing on all primary servers. ( no read replica server )
*   **placements:<_comma_separated_geo_locations_>** - For uniform load balancing on the servers qualified in the given placement list.



Please note there is an existing property called ‘loadBalanceHosts’ in Postgres. We chose not to use it and other properties to specify multiple servers because of the following reasons:



*   It takes a boolean value. More fine grained control anyways would have required a separate property.
*   A list of servers can change with time depending on how the cluster scales up and down or fails.

_How does it work:_



1. Connection url or property bag to contain ‘load-balance=true/false/placement’ property
2. First connection attempt creates a connection to the url given as is and fetches server information using the discovery query **“select * from yb_servers()’. **It then randomly chooses one from the obtained list of hosts and creates a new connection by just modifying the “PGHOST” property.
3. Subsequent connection creation chooses the least loaded server. The driver keeps track of the number of connections it has created on each server and hence knows about the least loaded server from it’s perspective.
4. Servers list can change with time because of many reasons. New servers added/removed from the cluster. Some servers crashed or became unreachable yet the cluster has enough quorum to continue operating. Therefore it is essential to refresh the server list frequently. The driver explicitly refreshes this information every time a new connection request comes to it if the information which it has is more than 5 minutes old.

###Failure Handling


Failure handling or server crash/unavailability handling.

Due to unavailability/crash of a server failures can come during the following executions.



1. A new connection creation
2. DDL execution on a connection
3. Single Statement execution - DMLs including _select_
4. Batch/LargeBatch Statement execution -_ _DMLs including _select_
5. Prepared Statement prepare phase -_ _DMLs including _select_
6. Prepared Statement execute phase -_ _DMLs including _select_
7. Prepared Statement batch execution -_ _DMLs including _select_
8. Callable Statements

Failure handling in all the above scenarios would mean some sort of automatic retry which is transparent to the user without compromising on correctness of the result or making the data dirty in the database. Idempotent operations are the best candidates for retrial.

Given that the client side does not maintain any application level state therefore automatic retrial of a multi statement transaction is impossible from the client layer and is best handled at the application layer itself.

_<span style="text-decoration:underline;">Three Key points for retry</span>_



1. The driver can use the ‘**auto-commit**’ property set on the connection to determine whether it should retry or not. Apart from failure during connection creation all other executions will be retried or not will depend on whether the ‘auto-commit’ property is enabled or not.
2. Several **session properties** are sometimes explicitly set on a connection. Automatically retrying on a new connection without setting the same session properties on the new connection can lead to unexpected behaviours. For example setting the timezone just for a session, or setting up a higher logging level etc. There are two approaches which can be thought of in this case. One, keep track of all the statements executed on a connection which starts with ‘SET’ and transparently replay them on the new connection before using it. Two, do not retry for such scenarios. Right now we will prefer not to retry for this use case. \

3. Only failures related to Connection exceptions i.e. Class 08 sqlstate exceptions or Class 53 sqlstate exceptions ( insufficient resources ) should trigger retries.

Failure handling in the below scenarios. The following section assumes that ‘auto-commit’ is enabled and session level properties have not been set explicitly on them. If any of the above conditions is not there then the failure will be propagated back up to the application level like the current behavior.



1. _A new connection creation_ - If it is the very first time nothing can be done. Throw the actual exception. For subsequent connections, if there is a failure during connection creation then blacklist the server, attempt another server from the cached list and also execute the ‘server discovery’ query and refresh the cache. Alternatively we can reverse the order also i.e. fetch the current state first and then choose one from the list obtained. \

2. _DDL execution on a connection _- All ddl executions in Yugabyte are atomic. Either they succeed or they fail. Assuming that the atomicity behavior is adhered to in Yugabyte it would be easy to catch the exception, check for the eligibility of retry based on the exception type and then create another connection for retry. The server list can be refreshed also at this time. \

3. _Single Statement execution _- If it is a ‘read’ operation then retry can be attempted similar to DDL execution. It will be risky to retry write operations on auto-commit as it will be difficult to deterministically know on the client side whether the operation went through or it failed before changing the database. Also, select queries on connections with scrollable cursors should not be reattempted as the client application may have taken some decision on consumed results. \

4. PreparedStatements - Prepared Statements for write again have the same issue as that of ‘writes’ ( insert/update/delete ) in the execution phase. Prepare phase can be retried for both ‘reads’ i.e. selects as well as ‘writes’ i.e. insert/delete/update. However in the execute phase if a client gets a disconnect exception then in case of reads it will create a new connection, re-prepare the statement on the new connection and then execute it. Please note, an application can have a gap between when it prepares the statement vs when it executes it. Retry here may confuse the application if the metadata from the prepare phase has already been consumed and the retry prepare is actually in a changed database state ( schema, plan etc ). For example in the meantime a column is added/dropped etc.  \

5. Callable Statements - No retries.

###Data Aware Query Routing


As noted in the motivation section above data aware routing of queries is important to ensure single hop execution resulting in better performance.

The basic idea in solving this is to determine the most appropriate server to which the execution should go such that remote fetch of data, or remote write of data is avoided/minimized.

The application thread normally works on a single connection and fires all it’s queries on it. The execution at the client end needs to be intercepted before it is actually sent to the server, the right server needs to be determined, and then an appropriate connection from an underlying pool of connections needs to be borrowed for execution. For prepared statements we can use the prepare phase to send back extra information from the server in response about the parameters and constants to be used in hash calculations.

####SQL Statements that qualify for data aware routing


Single-hop can be attempted for DMLS with the following sub-cases



1. DML SQL Statements ( INSERTS/UPDATES/DELETES/SELECTS )
   *   Transactions with auto-commit ON
   *   Transactions without auto-commit
2. DML Prepared Statements ( INSERTS/UPDATES/DELETES/SELECTS )
   *   Transactions with auto-commit ON
   *   Transactions without auto-commit
3. DML Batched SQL Statements
   *   Transactions with auto-commit ON
   *   Transactions without auto-commit
4. DML Batched Prepared Statements
   *   Transactions with auto-commit ON
   *   Transactions without auto-commit

####Main Components


Below are the main components needed to do data aware routing \




1. Parser - A richer parser on the client side is required. The existing parser at the client side is very minimalistic as the client side does not need elaborate parsing. However the smart driver needs this specially for non-prepared sql statements. Determining the column names, corresponding values and tables from the query is the first mandatory step for query routing. [JSQLParser](https://github.com/JSQLParser/JSqlParser) seems to be a good one for this purpose. As mentioned above for prepared statements we can piggyback on the response of prepare and send extra information about how the parameters and constants need to be used in calculating the hash value on the client side. So the parser can be avoided for prepared statements and should be more accurate. \

2. MetadataManager - A singleton instance which will keep the following information.
   1. Tablet locations of tables - both leader as well as the tablets
   2. Tablet locations of indexes - Indexes in Yugabyte-db are materialized indexes. If covering indexes are there and the projection list is totally covered then most likely the plan chosen will be index scan. So directly hopping to the tserver hosting the leader tablet of the index will be much useful than going to the base table leader tablet
   3. Data Types of the pk column(s) - The values obtained from parsing needs to be properly casted to the right data type before passing it to the hash function
   4. PreparedStatements information - Top level connection wise prepared statement information regarding
      1. Index parameters of the columns involved in determining hash value
      2. Statement to server mapping - indicating whether a prepared statement has been sent to a particular server or not. \

3. New virtual Tables or catalog functions similar to yb_servers() - Two new virtual tables or functions would be added. One to tell the mapping of _‘hash_value → tablet-id’ _and the other to store the _‘tablet_id → host:port of the tserver’ \
   _
4. Hash calculator - The client side needs to exactly mimic the hash value calculation on the server so that the right tablets can be targeted for single hop. \

5. Internal pool of connections - A pool of connections per server needs to be created and shared by all top level connections



####More details


Here are some of the low level changes needed. \




1. Fetching of meta information - This can be done in two ways.  \
   A) Lazily - When the first statement is executed from the client, the server will collect all the meta information and send it back as part of the response. The result part has a section for writing SQLWarnings if any at the server to be sent to the client. A special internal warning can be created with all the meta information and sent back. This should not require a lot of change. However this should be sent only when the server detects that a smart driver is connected else it will break the compatibility with existing drivers. The indication to the server can be given through an initial property while establishing the connection. \
   B) Periodically querying the new virtual tables to be added. ‘A’ seems to be the preferred approach. \

2. Transactions - Transactions on auto-commit sessions will be relatively easier to handle than multi statement one with auto-commit off.
   1. Auto-commit false - Transaction ids are generated on the server side. The client first sends a BEGIN message indicating the start of a transaction. The transaction id is generated on the server side at the first operation and then subsequent operations use the same transaction id. In case of single-hop transactions going to separate servers need to be given the same transaction id. Id generated for the first time needs to be sent with all subsequent operations going to different servers so that they are viewed as part of the same transaction on the server side. There are assertions on the server side where ‘backend-id’ part of the transactions are asserted for being equal to the backend-id of the local server which will not be true in this case. So we will have to make the necessary changes to avoid those assertions in this case. Commit and rollback needs to be sent to the same connection where the transaction started.
   2. Auto-commit true - Should be handled automatically.

###Sharing a single physical connection


This is not in the current scope.
