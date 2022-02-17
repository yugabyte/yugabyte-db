# Running CDC with the Java console client
We have one Java console client which will take the changes and just print it to the console. Do note that this client is strictly meant for testing purposes only.

### 1. Setup a cluster
  This can be done either locally using yugabyted or via Yugabyte Cloud. Take a look at the [Quick Start](https://docs.yugabyte.com/latest/quick-start/) guide to know more.
  
  Assuming that you have cloned the YugabyteDB repo from GitHub and built the code already, you can move to the next step. If not done already, follow these steps:
  1. Clone the repository
        
     ```bash
     git clone git@github.com:yugabyte/yugabyte-db.git
     ```
  2. Build the code
     ```bash
     cd yugabyte-db/
     
     ./yb_build.sh
     ```

  3. Once build is finished, it will generate a jar file under `java/yb-cdc/target/` with the name `yb-cdc-connector.jar`, this is our console client.

### 2. Start a local cluster if running locally
  
  ```bash
  ./bin/yugabyted start
  ```

### 3. Connect to ysqlsh

  ```bash
  ./bin/ysqlsh
  
  # Create a database once connected. You can use any database, as long as you have the permission for it. We will be using 'testdatabase' throughout these steps
  yugabyte=# CREATE DATABASE testdatabase;
  CREATE DATABASE
  yugabyte=# \c testdatabase
  You are now connected to database "testdatabase" as user "yugabyte".
  testdatabase=# CREATE TABLE test (id int primary key, name varchar(50), email text);
  CREATE TABLE
  ```
  
### 4. Use yb-admin to create a CDC stream

  ```bash
  ./build/latest/bin/yb-admin create_change_data_stream ysql.yugabyte
  CDC Stream ID: 0bb74adc723248d584ce90b856974633
  ```
  
### 5. Create a `config.properties` file with the following contents:
  
  ```bash
  vi ~/config.properties
  ```
  
  The contents should be:
  
  ```properties
  admin.operation.timeout.ms=30000
  operation.timeout.ms=30000
  num.io.threads=1
  socket.read.timeout.ms=30000
  table.name=test
  stream.id=0bb74adc723248d584ce90b856974633
  schema.name=testdatabase
  format=proto
  master.address=127.0.0.1\:7100
  ```
  
### 6. Run the JAR file
  This jar file will run our Java console client, you can directly view the changes in your terminal.
  
  ```bash
  java -jar java/yb-cdc/target/yb-cdc-connector.jar --config_file ~/config.properties
  ```
  
  Additionally, there are some flags associated with this jar file which can be used too:
  | **Option**                | **Default** | **Description**                                                                                                                                                                                                                                                                                                     |
|---------------------------|-------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| --help                    |             | Display the help.                                                                                                                                                                                                                                                                                                   |
| --show_config_file        |             | Display an example of a config.properties file.                                                                                                                                                                                                                                                                     |
| --master_address          |             | A list of comma separated values of master nodes in the form host:port                                                                                                                                                                                                                                              |
| --table_name              |             | This can be used to pass the table name. It can take two formats:<br/><br/>  `<namespace-name>.<table-name>` this format will assume that the schema of the table is public. <br/><br/>  `<namespace-name>.<schema-name>.<table-name>` this format takes into consideration if the table is under any other schema. |
| --stream_id               |             | The stream ID created using yb-admin can also be passed via this option.                                                                                                                                                                                                                                            |
| --format                  |    proto    | The format of the change records. We have 2 formats supported - `proto` and `json`                                                                                                                                                                                                                                  |
| --disable_snapshot        |             | This flag does not take any argument with it. If this flag is there, it will disable taking the snapshot of the table. By default, the console client will always take a snapshot of the table                                                                                                                      |
| --ssl_cert_file           |             | The root certificate against which the server is to be validated.                                                                                                                                                                                                                                                   |
| --ssl_client_cert         |             | Path to client certificate file.                                                                                                                                                                                                                                                                                    |
| --ssl_client_key          |             | Path to private key file for the client.                                                                                                                                                                                                                                                                            |
| --max_tablets             |      10     | The maximum number of tablets the client can poll for. Ideally this should be greater than or equal to the number of tablets there are for a table.                                                                                                                                                                 |
| --poll_interval           |     200     | The polling interval in milliseconds at which the client should request for the changes.                                                                                                                                                                                                                            |
| --create_new_db_stream_id |             | This flag does not take any arguments. If this is not provided, you won't need to create a DB stream ID using yb-admin, the console client would take care of that automatically. But it's still advisable that you use yb-admin rather than this flag.                                                             |
