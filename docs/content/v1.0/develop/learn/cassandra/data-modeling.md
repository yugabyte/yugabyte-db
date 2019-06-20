
## Keyspaces, tables, rows and columns

### Keyspaces

Cassandra keyspaces are a collection of tables. They are analogous to SQL namespaces. Typically, each application creates all its tables in one keyspace.

### Tables

A table is a collection of data. A keyspace most often contains one or more tables. Each table is identified by a name. Tables have a set of columns and contain records (rows) with data. Tables can be created, dropped, and altered at runtime without blocking updates and queries.

### Rows

Each table contains multiple rows of data. A row is a set of columns that is uniquely identifiable among all of the other rows.

### Columns

Each row is composed of one or more columns. A column is a fundamental data element, and does not need to be broken down any further.

As the example of a `users` table which holds information about users of a service.

| user_id  | firstname | lastname | address
| -------- | --------- | -------- | -------- 
| 1001     | Sherlock  | Holmes   | 221b Baker St, London, UK
| 1003     | Clark     | Kent     | 344 Clinton Street, Metropolis
| 1007     | James     | Bond     |

Note the following about the `users` table:

- Each row in the table has a unique value for the primary key column (`user_id`).
- Other than the primary key, the `users` table has three other columns - `firstname`, `lastname`, `address` each of which is a string.
- Some columns may have no data (for example, James Bond's address `address` is unknown). These have `null` values in the database.


Now consider another example of the `books` table that keeps track of authors and the books they have written.

| author               | book_title           | price  | year | genre
| -------------------- | -------------------- | ------ | ---- | ----- 
| William Shakespeare  | Hamlet               | 6.75   | 1602 | tragedy
| William Shakespeare  | Macbeth              | 7.50   | 1606 | tragedy
| Charles Dickens      | Oliver Twist         | 9.25   | 1837 | serial novel
| Charles Dickens      | A Tale of Two Cities | 11.40  | 1859 | historical novel


Note the following about the `books` table:

- The primary key for this table consists of two columns - `author` and `book_title`. Each row in the table must have values for these two attributes, and the combination of these values must be unique.
- Other than the primary key, the table has other columns such as `price`, `year`, `genre`.
- The columns `author`, `book_title` and `genre` are string, `price` is a float, `year` is an integer.


## Primary Key

When creating a table, the primary key of the table must be specified in addition to the table name. The primary key uniquely identifies each row in the table, therefore no two rows can have the same key.

There are two types of primary keys, and they are described below.

### Partition key columns (required)

Such tables have *simple primary keys*. One or more columns of a table can be made the partition key columns. The values of the partition key columns are used to compute an internal hash value. This hash value determines the tablet (or partition) in which the row will be stored. This has two implications:

- Each unique set of partition key values is hashed and distributed across nodes randomly to ensure uniform utilization of the cluster.

- All the data for a unique set of partition key values are always stored on the same node. This matters only if there are clustering key columns, which are described in the next section.

In the case of the `users` table, we can make `user_id` column the only primary key column. This is a good choice for a partition key because our queries do not care about the order of the `user_id`s. If the table is split into a number of tablets (partitions), the data may be assigned as follows.

| tablet    | user_id  | firstname | lastname | address
| --------- | -------- | --------- | -------- | -------- 
| tablet-22 | 1001     | Sherlock  | Holmes   | 221b Baker St, London, UK
| tablet-4  | 1003     | Clark     | Kent     | 344 Clinton Street, Metropolis
| tablet-17 | 1007     | James     | Bond     |


### Clustering key columns (optional)

The clustering columns specify the order in which the column data is sorted and stored on disk for a given unique partition key value. More than one clustering column can be specified, and the columns are sorted in the order they are declared in the clustering column. It is also possible to control the sort order (ascending or descending sort) for these columns. Note that the sort order respects the data type.

In a table that has both partition keys and clustering keys, it is possible for two rows to have the same partition key value and therefore they end up on the same node. However, those rows must have different clustering key values in order to satisfy the primary key requirements.

In the case of the `books` table, `author` is a good partition key and `book_title` is a good clustering key. Such a data model would allow easily listing all the books for a given author, as well as look up details of a specific book. This would cause the data to be stored as follows.

| tablet    | author               | book_title           | price  | year | genre
| --------- | -------------------- | -------------------- | ------ | ---- | ----- 
| tablet-15 | William Shakespeare  | Hamlet               | 6.75   | 1602 | tragedy
| tablet-15 | William Shakespeare  | Macbeth              | 7.50   | 1606 | tragedy
| tablet-21 | Charles Dickens      | A Tale of Two Cities | 11.40  | 1859 | historical novel
| tablet-21 | Charles Dickens      | Oliver Twist         | 9.25   | 1837 | serial novel


Note that if we had made both `author` and `book_title` partition key columns, we would not be able to list all the books for a given author efficiently.

**Note**

- The partition key columns are also often referred to as its  *hash columns*. This is because an internal hash function is used to distribute data items across tablets based on their partition key values.

- The clustering key columns are also referred to as its **range columns**. This is because rows with the same partition key are stored on disk in sorted order by the clustering key value.


