## Step 1. Clone the YCSB repository

You can do this by running the following commands.

```sh
cd $HOME
git clone https://github.com/yugabyte/YCSB.git
cd YCSB
```

## Step 2. Compile the code

You can compile the code using the following commands.

```sh
mvn clean package
```

We can also just compile the yugabyteCQL binding using:

```sh
mvn -pl yugabyteCQL -am clean package -DskipTests
```

## Step 3. Start your database
Start the database using steps mentioned here: https://docs.yugabyte.com/latest/quick-start/explore-ysql/.

## Step 4. Configure your database and table
Create the Database and table using the cqlsh tool.
The cqlsh tool is distributed as part of the database package.

```sh
bin/cqlsh <ip> --execute "create keyspace ycsb"
bin/cqlsh <ip> --keyspace ycsb --execute 'create table usertable (y_id varchar primary key, field0 varchar, field1 varchar, field2 varchar, field3 varchar, field4 varchar, field5 varchar, field6 varchar,  field7 varchar, field8 varchar, field9 varchar);'
```

## Step 5. Configure YCSB connection properties
Set the following connection configurations in yugabyteCQL/db.properties:

```sh
hosts=<ip>
port=9042
cassandra.username=yugabyte
```

The other configuration parameters like username, password, connection
parameters, etc. are described in detail at [this page](https://github.com/yugabyte/YCSB/tree/master/yugabyteCQL)

## Step 6. Running the workload
Before you can actually run the workload, you need to "load" the data first.

```sh
bin/ycsb load yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloada
```

Then, you can run the workload:

```sh
bin/ycsb run yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloada
```

To run the other workloads, say workloadb, all we need to do is change that argument in the above command.

```sh
bin/ycsb run yugabyteCQL -P yugabyteCQL/db.properties -P workloads/workloadb
```
