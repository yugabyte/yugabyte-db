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

We can also just compile the yugabyteSQL binding using:

```sh
mvn -pl yugabyteSQL -am clean package -DskipTests
```

## Step 3. Start your database
Start the database using steps mentioned here: https://docs.yugabyte.com/latest/quick-start/explore-ysql/.

## Step 4. Configure your database and table
Create the Database and table using the ysqlsh tool.
The ysqlsh tool is distributed as part of the database package.

```sh
bin/ysqlsh -h <ip> -c 'create database ycsb;'
bin/ysqlsh -h <ip> -d ycsb -c 'CREATE TABLE usertable (YCSB_KEY VARCHAR(255) PRIMARY KEY, FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT, FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT, FIELD8 TEXT, FIELD9 TEXT);'
```

## Step 5. Configure YCSB connection properties
Set the following connection configurations in yugabyteSQL/db.properties:

```sh
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://<ip>:5433/ycsb;
db.user=yugabyte
db.passwd=
```

The other configuration parameters, are described in detail at [this page](https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties)

## Step 6. Running the workload
Before you can actually run the workload, you need to "load" the data first.

```sh
bin/ycsb load yugabyteSQL -P yugabyteSQL/db.properties -P workloads/workloada
```

Then, you can run the workload:

```sh
bin/ycsb run yugabyteSQL -P yugabyteSQL/db.properties -P workloads/workloada
```

To run the other workloads, say workloadb, all we need to do is change that argument in the above command.

```sh
bin/ycsb run yugabyteSQL -P yugabyteSQL/db.properties -P workloads/workloadb
```
