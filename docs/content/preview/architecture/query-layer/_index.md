---
title: YugabyteDB Query Layer (YQL)
headerTitle: Query layer
linkTitle: YQL - Query layer
description: Understand how a query is processed
image: fa-sharp fa-thin fa-language
aliases:
  - /preview/architecture/query-layer/overview/
menu:
  preview:
    identifier: architecture-query-layer
    parent: architecture
    weight: 500
showRightNav: true
type: indexpage
---


The YugabyteDB Query Layer (YQL) is the primary layer that provides interfaces for applications to interact with using client drivers. This layer deals with the API-specific aspects such as query and command compilation, as well as the runtime functions such as data type representations, built-in operations, and so on. From the application perspective, YQL is stateless and the clients can connect to one or more YB-TServers on the appropriate port to perform operations against a YugabyteDB cluster.

![Query layer](/images/architecture/query_layer.png)

Although YQL is designed with extensibility in mind, allowing for new APIs to be added, it currently supports two types of distributed SQL APIs: [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/).

- [YSQL](../../api/ysql/) is a distributed SQL API that is built by reusing the PostgreSQL language layer code. It is a stateless SQL query engine that is wire-format compatible with PostgreSQL. The default port for YSQL is `5433`.
- [YCQL](../../api/ycql/) is a semi-relational language that has its roots in Cassandra Query Language. It is a SQL-like language built specifically to be aware of the clustering of data across nodes. The default port for YCQL is `9042`.

## Query processing

The primary function of the query layer is to process the queries sent by an application. The YQL processes the queries sent by an application in phases via four internal components.

{{<tip>}}
It's important to note that you don't need to worry about these internal processes. YugabyteDB automatically handles them when you submit a query.
{{</tip>}}

### Parser

The parser processes each query in several steps as follows:

1. Checks the query: The parser first checks if the query is written correctly and follows the proper SQL syntax rules. If there are any syntax errors, it returns an error message.

1. Builds a parse tree: If the query is written correctly, the parser builds a structured representation of the query, called a parse tree. This parse tree captures the different parts of the query and how they are related.

1. Recognizes keywords and identifiers: To build the parse tree, the parser first identifies the different components of the query, such as keywords (like `SELECT`, `FROM`), table or column names, and other identifiers.

1. Applies grammar rules: The parser then applies a set of predefined grammar rules to understand the structure and meaning of the query based on the identified components.

1. Runs semantic analysis: After building the parse tree, the parser performs a semantic analysis to understand the detailed meaning of the query. It looks up information about the tables, columns, functions, and operators referenced in the query to ensure they exist and are being used correctly.

1. Creates a query tree: The semantic analysis step creates a new data structure called the query tree, which represents the complete, semantically understood version of the query.

The reason for separating the initial parsing and the semantic analysis is to allow certain types of queries (like transaction control commands) to be executed quickly without the need for a full semantic analysis. The query tree contains more detailed information about data types, functions, and expressions used in the query, making it easier for the system to execute the query correctly.

### Analyzer

The created query tree is then analyzed, rewritten, and transformed based on any rules stored in the system catalog.

Views are realized during this phase. Whenever a query against a view (that is, a virtual table) is made, the original query is rewritten to a query that accesses the base tables given in the view definition instead.

### Planner

YugabyteDB needs to determine the most efficient way to execute a query and return the results. This process is handled by the query planner/optimizer component.

The planner first analyzes different ways a query can be executed based on the available data and indexes. It considers various strategies like scanning tables sequentially or using indexes to quickly locate specific data.

If the query involves joining multiple tables, the planner evaluates different techniques to combine the data:

- Nested loop join: Scanning one table for each row in the other table. This can be efficient if one table is small or has a good index.
- Merge join: Sorting both tables by the join columns and then merging them in parallel. This works well when the tables are already sorted or can be efficiently sorted.
- Hash join: Building a hash table from one table and then scanning the other table to find matches in the hash table.
For queries involving more than two tables, the planner considers different sequences of joining the tables to find the most efficient approach.

The planner estimates the cost of each possible execution plan and chooses the one expected to be the fastest, taking into account factors like table sizes, indexes, sorting requirements, and so on.

After the optimal plan is determined, YugabyteDB generates a detailed execution plan with all the necessary steps, such as scanning tables, joining data, filtering rows, sorting, and computing expressions. This execution plan is then passed to the query executor component, which carries out the plan and returns the final query results.

{{<note>}}
The execution plans are cached for prepared statements to avoid overheads associated with repeated parsing of statements.
{{</note>}}

### Executor

After the query planner determines the optimal execution plan, the query executor component runs the plan and retrieves the required data. The executor sends appropriate requests to the other YB-TServers that hold the needed data to performs sorts, joins, aggregations, and then evaluates qualifications and finally returns the derived rows.

The executor works in a step-by-step fashion, recursively processing the plan from top to bottom. Each node in the plan tree is responsible for fetching or computing rows of data as requested by its parent node.

For example, if the top node is a "Merge Join" node, it first requests rows from its two child nodes (the left and right inputs to be joined). The executor recursively calls the child nodes to get rows from them.

A child node may be a "Sort" node, which requests rows from its child, sorts them, and returns the sorted rows. The bottom-most child could be a "Sequential Scan" node that reads rows directly from a table.

As the executor requests rows from each node, that node fetches or computes the rows from its children, applies any filtering or data transformations specified in the query plan, and returns the requested rows up to its parent node.

This process continues recursively until the top node has received all the rows it needs to produce the final result. For a `SELECT` query, these final rows are sent to the client. For data modification queries like `INSERT`, `UPDATE`, or `DELETE`, the rows are used to make the requested changes in the database tables.

The executor is designed to efficiently pull rows through the pipeline defined by the plan tree, processing rows in batches where possible for better performance.

