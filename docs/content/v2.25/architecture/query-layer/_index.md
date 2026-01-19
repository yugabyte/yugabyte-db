---
title: YugabyteDB Query Layer (YQL)
headerTitle: Query layer
linkTitle: YQL - Query layer
description: Understand how a query is processed
menu:
  v2.25:
    identifier: architecture-query-layer
    parent: architecture
    weight: 500
showRightNav: true
type: indexpage
---


The YugabyteDB Query Layer (YQL) is the primary layer that provides interfaces for applications to interact with using client drivers. This layer deals with the API-specific aspects such as query and command compilation, as well as the runtime functions such as data type representations, built-in operations, and so on. From the application perspective, YQL is stateless and the clients can connect to one or more YB-TServers on the appropriate port to perform operations against a YugabyteDB cluster.

![Query layer](/images/architecture/query_layer.png)

Although YQL is designed with extensibility in mind, allowing for new APIs to be added, it currently supports two types of distributed SQL APIs: [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/).

- [YSQL](../../api/ysql/) is a distributed SQL API that is built by reusing the PostgreSQL language layer code. It is a stateless SQL query engine that is wire-format compatible with PostgreSQL. The default port for YSQL is 5433.
- [YCQL](../../api/ycql/) is a semi-relational language that has its roots in Cassandra Query Language. It is a SQL-like language built specifically to be aware of the clustering of data across nodes. The default port for YCQL is 9042.

## Query processing

The primary function of the query layer is to process the queries sent by an application. The YQL processes the queries sent by an application in phases via four internal components.

{{<tip>}}
It's important to note that you don't need to worry about these internal processes. YugabyteDB automatically handles them when you submit a query.
{{</tip>}}

### Parser

The parser processes each query in several steps as follows:

1. Checks the query: The parser first checks if the query is written correctly and follows the proper SQL syntax rules. If there are any syntax errors, it returns an error message.

1. Builds a parse tree: If the query is written correctly, the parser builds a structured representation of the query, called a parse tree. This parse tree captures the different parts of the query and how they are related.

1. Recognizes keywords and identifiers: To build the parse tree, the parser first identifies the different components of the query, such as keywords (like SELECT, FROM), table or column names, and other identifiers.

1. Applies grammar rules: The parser then applies a set of predefined grammar rules to understand the structure and meaning of the query based on the identified components.

1. Runs semantic analysis: After building the parse tree, the parser performs a semantic analysis to understand the detailed meaning of the query. It looks up information about the tables, columns, functions, and operators referenced in the query to ensure they exist and are being used correctly.

1. Creates a query tree: The semantic analysis step creates a new data structure called the query tree, which represents the complete, semantically understood version of the query.

The reason for separating the initial parsing and the semantic analysis is to allow certain types of queries (like transaction control commands) to be executed quickly without the need for a full semantic analysis. The query tree contains more detailed information about data types, functions, and expressions used in the query, making it easier for the system to execute the query correctly.

### Analyzer

The created query tree is then analyzed, rewritten, and transformed based on any rules stored in the system catalog.

Views are realized during this phase. Whenever a query against a view (that is, a virtual table) is made, the original query is rewritten to a query that accesses the base tables given in the view definition instead.

### Planner

The YugabyteDB query planner plays a crucial role in efficiently executing SQL queries across multiple nodes. It extends the capabilities of the traditional single node query planner to handle distributed data and execution.

The planner first analyzes different ways a query can be executed based on the available data and indexes. It considers various strategies like scanning tables sequentially or using indexes to quickly locate specific data.

After determining the optimal plan, the planner generates a detailed execution plan with all the necessary steps, such as scanning tables, joining data, filtering rows, sorting, and computing expressions.

The execution plan is then passed to the query executor component, which carries out the plan and returns the final query results.

{{<lead link="./planner-optimizer/">}}
To learn how the query planner decides the optimal path for query execution, see [Query Planner](./planner-optimizer/)
{{</lead>}}

### Executor

After the query planner determines the optimal execution plan, the executor runs the plan and retrieves the required data. The executor sends requests to the other YB-TServers that hold the data needed to perform sorts, joins, and aggregations, then evaluates qualifications, and finally returns the derived rows.

The executor works in a step-by-step fashion, recursively processing the plan from top to bottom. Each node in the plan tree is responsible for fetching or computing rows of data as requested by its parent node.

For example, if the top node is a "Merge Join" node, it first requests rows from its two child nodes (the left and right inputs to be joined). The executor recursively calls the child nodes to retrieve rows.

A child node may be a "Sort" node, which requests rows from its child, sorts them, and returns the sorted rows. The bottom-most child could be a "Sequential Scan" node that reads rows directly from a table.

As the executor requests rows from each node, that node fetches or computes the rows from its children, applies any filtering or data transformations specified in the query plan, and returns the requested rows up to its parent node.

This process continues recursively until the top node has received all the rows it needs to produce the final result. For a SELECT query, these final rows are sent to the client. For data modification queries like INSERT, UPDATE, or DELETE, the rows are used to make the requested changes in the database tables.

The executor is designed to efficiently pull rows through the pipeline defined by the plan tree, processing rows in batches where possible for better performance.

### Optimizations

- **Incremental sort**. If an intermediate query result is known to be sorted by one or more leading keys of a required sort ordering, the additional sorting can be done considering only the remaining keys, if the rows are sorted in batches that have equal leading keys.

- **Memoize results**. When only a small percentage of rows is checked on the inner side of a nested-loop join, the executor memoizes the results for improving performance.

- **Disk-based hash aggregation**. Hash-based operations are generally more sensitive to memory availability and are highly efficient as long as the hash table fits within the memory specified by the work_mem parameter. When the hash table grows beyond the `work_mem` limit, the planner transitions to a disk-based hash aggregation plan. This avoids overloading memory and ensures that large datasets can be handled efficiently.

## Query ID

In YSQL, to provide a consistent way to track and identify specific queries across different parts of the system such as logs, performance statistics, and EXPLAIN plans, a unique identifier is generated for each query processed. The query ID is effectively a hash value based on the normalized form of the SQL query. This normalization process removes insignificant whitespace and converts literal values to placeholders, ensuring that semantically identical queries have the same ID. This provides the following benefits:

- By providing a unique identifier for each query, it becomes much easier to analyze query performance and identify problematic queries.
- Including query IDs in logs and performance statistics enables more detailed and accurate monitoring of database activity.
- The EXPLAIN command, which shows the execution plan for a query, can also display the query ID. This helps to link the execution plan with the actual query execution statistics.
- The pg_stat_statements extension (which is installed by default in YugabyteDB) can accurately track and report statistics even for queries with varying literal values (for example, different WHERE clause parameters). This makes it much easier to identify performance bottlenecks caused by specific query patterns.

Generation of this unique query ID is controlled using the `compute_query_id` setting, which can have the following values:

- on - Always compute query IDs.
- off - Never compute query IDs.
- auto (the default) - Automatically compute query IDs when needed, such as when pg_stat_statements is enabled (pg_stat_statements is enabled by default).

You should enable `compute_query_id` to fully realize its benefits for monitoring and performance analysis.
