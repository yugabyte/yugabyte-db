// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.loadtest;

import org.apache.spark.SparkConf;
import org.apache.spark.SparkContext;
import org.apache.spark.sql.Dataset;
import org.apache.spark.sql.Row;
import org.apache.spark.sql.SaveMode;
import org.apache.spark.sql.SparkSession;

import org.junit.Test;
import org.junit.runner.RunWith;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.net.InetSocketAddress;
import java.net.URL;
import java.util.Iterator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

import org.yb.minicluster.BaseMiniClusterTest;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

import com.datastax.oss.driver.api.core.CqlSession;
import com.datastax.oss.driver.api.core.cql.ResultSet;
import com.datastax.spark.connector.cql.CassandraConnector;
import com.datastax.spark.connector.CassandraSparkExtensions;

@RunWith(value=YBTestRunner.class)
public class TestSpark3Jsonb extends BaseMiniClusterTest {

  private Logger logger = LoggerFactory.getLogger(TestSpark3Jsonb.class);
  private static String KEYSPACE = "test";
  private static String INPUT_TABLE = "person";
  private static String CDR_TABLE = "cdr";
  private String phones[] = {
    "{\"code\":\"+42\",\"phone\":1000}",
    "{\"code\":\"+43\",\"phone\":1200}",
    "{\"code\":\"+44\",\"phone\":1400}",
    "{\"code\":\"+45\",\"phone\":1500,\"key\":[0,{\"m\":[12, -1, {\"b\":400}, 500]}]}",
    "{\"code\":\"+46\",\"phone\":1600}",
  };
  private String phoneOut[] = {
    "\"+42\"",
    "\"+43\"",
    "\"+44\"",
    "\"+45\""
  };
  String tableWithKeysapce = KEYSPACE + "." + INPUT_TABLE;

  @Test
  public void testJsonb() throws Exception {
      // Set up config.
      List<InetSocketAddress> addresses = miniCluster.getCQLContactPoints();

      // Setup the local spark master
      SparkConf conf = new SparkConf().setAppName("yb.spark-jsonb")
        .setMaster("local[1]")
        .set("spark.cassandra.connection.localDC", "datacenter1")
        .set("spark.cassandra.connection.host", addresses.get(0).getHostName())
        .set("spark.sql.catalog.mycatalog",
          "com.datastax.spark.connector.datasource.CassandraCatalog");

      CqlSession session = createTestSchemaIfNotExist(conf);

      SparkSession spark = SparkSession.builder().config(conf)
        .withExtensions(new CassandraSparkExtensions()).getOrCreate();

      Map<Integer, String> expectedValues = new HashMap<>();
      expectedValues.put(1, "Hammersmith London, UK");
      expectedValues.put(2, "Acton London, UK");
      expectedValues.put(3, "11 Acton London, UK");
      expectedValues.put(4, "4 Act London, UK");

      String query = "SELECT id, address, get_json_object(phone, '$.key[1].m[2].b') as key " +
                    "FROM mycatalog.test.person " +
                    "WHERE get_json_string(phone, '$.key[1].m[2].b') = '400' order by id limit 2";

      Dataset<Row> explain_rows = spark.sql("EXPLAIN " + query);
      Iterator<Row> iterator = explain_rows.toLocalIterator();
      StringBuilder explain_sb = new StringBuilder();
      while (iterator.hasNext()) {
          Row row = iterator.next();
          explain_sb.append(row.getString(0));
      }
      String explain_text = explain_sb.toString();
      logger.info("plan is " + explain_text);
      // check that column pruning works
      assertTrue(explain_text.contains("id,address,phone->'key'->1->'m'->2->'b'"));
      // check that jsonb column filter is pushed down
      assertTrue(explain_text.contains("phone->'key'->1->'m'->2->>'b'"));

      Dataset<Row> rows = spark.sql(query);

      iterator = rows.toLocalIterator();
      int rows_count = 1;
      while (iterator.hasNext()) {
          Row row = iterator.next();
          Integer id = row.getInt(0);
          String addr = row.getString(1);
          String jsonb = row.getString(2);
          assertEquals(jsonb, "400");
          assertTrue(expectedValues.containsKey(id));
          assertEquals(expectedValues.get(id), addr);
          rows_count++;
      }
      // TODO to update a JSONB sub-object only using Spark SQL --
      // for now requires using the Cassandra session directly.
      String update = "update " + tableWithKeysapce +
          " set phone->'key'->1->'m'->2->'b'='320' where id=4";
      session.execute(update);

      Dataset<org.apache.spark.sql.Row> df = spark.sqlContext().read()
          .format("org.apache.spark.sql.cassandra")
          .option("keyspace", KEYSPACE)
          .option("table", INPUT_TABLE).load();
      logger.info("Table loaded Sucessfully");

      df.createOrReplaceTempView("temp");

      spark.sqlContext().sql("select * from temp").show(false);
      logger.info("Data READ SuccessFully");

      session.close();
      spark.close();
  }

  @Test
  public void testCount() throws Exception {
      // Set up config.
      List<InetSocketAddress> addresses = miniCluster.getCQLContactPoints();

      // Setup the local spark master
      SparkConf conf = new SparkConf().setAppName("yb.spark-jsonb")
        .setMaster("local[1]")
        .set("spark.cassandra.connection.localDC", "datacenter1")
        .set("spark.cassandra.connection.host", addresses.get(0).getHostName())
        .set("spark.sql.catalog.mycatalog",
          "com.datastax.spark.connector.datasource.CassandraCatalog");

      CqlSession session = createTestSchemaIfNotExist(conf);
      SparkSession spark = SparkSession.builder().config(conf)
        .withExtensions(new CassandraSparkExtensions()).getOrCreate();

      String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS " + KEYSPACE + ";";
      session.execute(createKeyspace);
      String tbl = KEYSPACE + ".test_types";
      session.execute("create table " + tbl + "(domain text, open_date date, " +
        "subentity text, PRIMARY KEY ((domain,open_date), subentity) ) WITH CLUSTERING ORDER BY (" +
        "subentity DESC) AND default_time_to_live=15768000 AND transactions = {'enabled': 'true'}");
      session.execute("insert into " + tbl + " (domain, open_date, subentity)" +
        "values ('a', '2021-7-29', 'c')");
      session.execute("insert into " + tbl + " (domain, open_date, subentity)" +
        "values ('a', '2021-7-28', 'c')");
      session.execute("insert into " + tbl + " (domain, open_date, subentity)" +
        "values ('b', '2021-7-27', 'c')");
      session.execute("insert into " + tbl + " (domain, open_date, subentity)" +
        "values ('a', '2021-7-26', 'c')");

      String query = "select count(*) from " + "mycatalog."+ tbl +
        " where domain='a' and open_date='2021-7-29'";
      Dataset<Row> explain_rows = spark.sql("EXPLAIN " + query);
      Iterator<Row> iterator = explain_rows.toLocalIterator();
      StringBuilder explain_sb = new StringBuilder();
      while (iterator.hasNext()) {
          Row row = iterator.next();
          explain_sb.append(row.getString(0));
      }
      String explain_text = explain_sb.toString();
      logger.info("plan is " + explain_text);

      Dataset<Row> rows = spark.sql(query);
      iterator = rows.toLocalIterator();
      int rows_count = 1;
      while (iterator.hasNext()) {
          Row row = iterator.next();
          Long id = row.getLong(0);
          logger.info("cnt = " + id);
          assertTrue(id == 1);
      }
  }

  @Test
  public void testCdr() throws Exception {
      // Set up config.
      List<InetSocketAddress> addresses = miniCluster.getCQLContactPoints();

      // Setup the local spark master
      SparkConf conf = new SparkConf().setAppName("yb.spark-jsonb")
        .setMaster("local[1]")
        .set("confirm.truncate", "true")
        .set("spark.cassandra.output.ignoreNulls", "true")
        .set("spark.cassandra.connection.localDC", "datacenter1")
        .set("spark.cassandra.connection.host", addresses.get(0).getHostName())
        .set("spark.sql.catalog.mycatalog",
          "com.datastax.spark.connector.datasource.CassandraCatalog");

      createSchemaIfNotExist(conf);
      SparkSession spark = SparkSession.builder().config(conf)
        .withExtensions(new CassandraSparkExtensions()).getOrCreate();
      processCDRForYoutube(spark);

      String query = "SELECT usage from mycatalog.test.cdr where imsi=116 order by gdate";
      Dataset<Row> rows = spark.sql(query);
      Iterator<Row> iterator = rows.toLocalIterator();
      int count = 0;
      while (iterator.hasNext()) {
          Row row = iterator.next();
          String jsonb = row.getString(0);
          assertEquals(jsonb, "{\"dl\":1122,\"rsrq\":10000,\"ul\":72}");
          count++;
      }
      processCDRForCall(spark);

      Dataset<org.apache.spark.sql.Row> df = spark.sqlContext().read()
          .format("org.apache.spark.sql.cassandra")
          .option("keyspace", KEYSPACE)
          .option("table", CDR_TABLE).load();
      logger.info("Table loaded Sucessfully");

      df.createOrReplaceTempView("temp");

      spark.sqlContext().sql("select * from temp").show(false);
      logger.info("Data READ SuccessFully");

      query = "SELECT usage, nm from mycatalog.test.cdr where imsi=116 order by gdate";
      rows = spark.sql(query);
      iterator = rows.toLocalIterator();
      while (iterator.hasNext()) {
          Row row = iterator.next();
          String jsonb = row.getString(0);
          assertEquals(jsonb,
            "{\"dl\":1122,\"rsrp\":\"s80\",\"rsrq\":10000,\"sinr\":\"100.1\",\"ul\":72}");
          assertEquals(true, row.isNullAt(1));
      }
      spark.close();
  }

  private void processCDRForCall(SparkSession spark) {
    URL sqlFileRes = getClass().getClassLoader().getResource("cdr-call.csv");
    Dataset<Row> rows = spark.read().option("header", true).csv(sqlFileRes.getFile());
    rows.show();
    rows.createOrReplaceTempView("temp");
    Dataset<Row> updatedRows = spark
        .sql("select date as gdate,imsi,nm,rsrp,sinr,rsrq from temp ");
    updatedRows.show();
    updatedRows.write().format("org.apache.spark.sql.cassandra").option("keyspace", KEYSPACE)
        .option("table", CDR_TABLE)
        .option("spark.cassandra.json.quoteValueString", "true")
        .option("spark.cassandra.mergeable.json.column.mapping", "dl,rsrp,sinr,ul,rsrq:usage")
        .mode(SaveMode.Append).save();
  }
  private void processCDRForYoutube(SparkSession spark) {

    URL sqlFileRes = getClass().getClassLoader().getResource("cdr-youtube.csv");
    Dataset<Row> rows = spark.read().option("header", true).csv(sqlFileRes.getFile());
    rows.show();
    long start = System.currentTimeMillis();
    rows.createOrReplaceTempView("temp");
    spark.sqlContext().sql("select * from temp").show(false);
    logger.info("starting youtube");
    Dataset<Row> updatedRows = spark
      .sql("select date as gdate,imsi,dl,ul,rsrq from temp ");
    updatedRows.show();
    logger.info("starting youtube");
    updatedRows.write().format("org.apache.spark.sql.cassandra").option("keyspace", KEYSPACE)
        .option("table", CDR_TABLE)
        .option("spark.cassandra.mergeable.json.column.mapping", "dl,rsrp,sinr,ul,rsrq:usage")
        .mode(SaveMode.Append).save();
    logger.info("df save took " + (System.currentTimeMillis()-start) + "; " + updatedRows.count());

  }

  private void createSchemaIfNotExist(SparkConf conf) {
    CassandraConnector connector = CassandraConnector.apply(conf);

    try (CqlSession session = connector.openSession();) {

      String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS " + KEYSPACE + ";";
      session.execute(createKeyspace);
      logger.info("Created keyspace name: {}", KEYSPACE);

      String tableWithKeysapce = KEYSPACE + "." + CDR_TABLE;

      String createTable = "CREATE TABLE IF NOT EXISTS " + tableWithKeysapce +
          "(gdate date, imsi text, nm int," + "usage jsonb, PRIMARY KEY(gdate,imsi) )";

      session.execute(createTable);
      logger.info("Created table tablename: {}", INPUT_TABLE);
      session.close();
    } catch (Exception e) {
      logger.error("Error in creating schema: {}", e.getStackTrace());
      e.printStackTrace();
    }
  }
  private CqlSession createTestSchemaIfNotExist(SparkConf conf) throws Exception {
      // Create the Cassandra connector to Spark.
      CassandraConnector connector = CassandraConnector.apply(conf);

      // Create a Cassandra session, and initialize the keyspace.
      CqlSession session = connector.openSession();

      String createKeyspace = "CREATE KEYSPACE IF NOT EXISTS " + KEYSPACE + ";";
      session.execute(createKeyspace);
      logger.info("Created keyspace name: {}", KEYSPACE);

      // Create table 'employee' if it does not exist.
      String createTable = "CREATE TABLE IF NOT EXISTS " + tableWithKeysapce
          + "(id int PRIMARY KEY,"
          +"name text,"
          +"address text,"
          +"phone jsonb"
          +");";

      session.execute(createTable);
      logger.info("Created table tablename: {}", INPUT_TABLE);

      // Insert a row.
      String insert = "INSERT INTO "+tableWithKeysapce
        + "(id, name, address,  phone) VALUES (1, 'John', 'Hammersmith London, UK','"
        + phones[0] + "');";
      session.execute(insert);
      insert = "INSERT INTO "+tableWithKeysapce
        +"(id, name, address,  phone) VALUES (2, 'Nick', 'Acton London, UK','" + phones[1] + "');";
      session.execute(insert);

      insert = "INSERT INTO "+tableWithKeysapce
        + "(id, name, address,  phone) VALUES (3, 'Smith', '11 Acton London, UK','"
        + phones[2] + "');";
      session.execute(insert);

      insert = "INSERT INTO "+tableWithKeysapce
        + "(id, name, address,  phone) VALUES (4, 'Kumar', '4 Act London, UK','"
        + phones[3] + "');";
      session.execute(insert);

      logger.info("Inserted data: {}" , insert);
      return session;
  }
}
