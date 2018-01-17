//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/yql/cql/ql/test/ql-test-base.h"

namespace yb {
namespace master {
class CatalogManager;
class Master;
}
namespace ql {

#define EXEC_DUPLICATE_TABLE_CREATE_STMT(ql_stmt)                                                 \
do {                                                                                               \
  Status s = processor->Run(ql_stmt);                                                             \
  EXPECT_FALSE(s.ok());                                                                            \
  EXPECT_FALSE(s.ToString().find("Duplicate Table. Already present") == string::npos);            \
} while (false)

#define EXEC_INVALID_TABLE_CREATE_STMT(ql_stmt, msg)                                              \
do {                                                                                               \
  Status s = processor->Run(ql_stmt);                                                             \
  ASSERT_FALSE(s.ok());                                                                            \
  ASSERT_FALSE(s.ToString().find(msg) == string::npos);                                            \
} while (false)

class TestQLCreateTable : public QLTestBase {
 public:
  TestQLCreateTable() : QLTestBase() {
  }

  inline const string CreateStmt(string params) {
    return "CREATE TABLE " + params;
  }

  inline const string CreateIfNotExistsStmt(string params) {
    return "CREATE TABLE IF NOT EXISTS " + params;
  }
};

TEST_F(TestQLCreateTable, TestQLCreateTableSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "human_resource1(id int, name varchar, primary key(id));";
  const string table2 = "human_resource2(id int primary key, name varchar);";
  const string table3 = "human_resource3(id int, name varchar primary key);";
  const string table4 = "human_resource4(id int, name varchar, primary key(id, name));";
  const string table5 = "human_resource5(id int, name varchar, primary key((id), name));";
  const string table6 =
      "human_resource6(id int, name varchar, salary int, primary key((id, name), salary));";

  const string table7 = "human_resource7(id int, name varchar, primary key(id));";
  const string table8 = "human_resource8(id int primary key, name varchar);";
  const string table9 = "human_resource9(id int, name varchar primary key);";
  const string table10 = "human_resource10(id int, name varchar, primary key(id, name));";
  const string table11 = "human_resource11(id int, name varchar, primary key((id), name));";
  const string table12 =
      "human_resource12(id int, name varchar, salary int, primary key((id, name), salary));";

  // Define primary key before defining columns.
  const string table13 =
      "human_resource13(id int, primary key((id, name), salary), name varchar, salary int);";

  // Create the table 1.
  EXEC_VALID_STMT(CreateStmt(table1));

  // Create the table 2. Use "id" as primary key.
  EXEC_VALID_STMT(CreateStmt(table2));

  // Create the table 3. Use "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table3));

  // Create the table 4. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table4));

  // Create the table 5. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table5));

  // Create the table 6. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table6));;

  // Create table 7.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));

  // Create the table 8. Use "id" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));

  // Create the table 9. Use "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));

  // Create the table 10. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));

  // Create the table 11. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));

  // Create the table 12. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  // Create the table 13. Define primary key before the columns.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table13));

  // Verify that all 'CREATE TABLE' statements fail for tables that have already been created.
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table1));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table2));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table3));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table4));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table5));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table6));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table7));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table8));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table9));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table10));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table11));
  EXEC_DUPLICATE_TABLE_CREATE_STMT(CreateStmt(table12));

  // Verify that all 'CREATE TABLE IF EXISTS' statements succeed for tables that have already been
  // created.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table1));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table2));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table3));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table4));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table5));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table6));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  const string drop_stmt = "DROP TABLE human_resource1;";
  EXEC_VALID_STMT(drop_stmt);
}

TEST_F(TestQLCreateTable, TestQLCreateTableWithTTL) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table 1.
  const string table1 = "human_resource100(id int, name varchar, PRIMARY KEY(id));";
  EXEC_VALID_STMT(CreateStmt(table1));

  EXEC_VALID_STMT("CREATE TABLE table_with_ttl (c1 int, c2 int, c3 int, PRIMARY KEY(c1)) WITH "
                      "default_time_to_live = 1;");

  // Query the table schema.
  master::Master *master = cluster_->mini_master()->master();
  master::CatalogManager *catalog_manager = master->catalog_manager();
  master::GetTableSchemaRequestPB request_pb;
  master::GetTableSchemaResponsePB response_pb;
  request_pb.mutable_table()->mutable_namespace_()->set_name(kDefaultKeyspaceName);
  request_pb.mutable_table()->set_table_name("table_with_ttl");

  // Verify ttl was stored in syscatalog table.
  CHECK_OK(catalog_manager->GetTableSchema(&request_pb, &response_pb));
  const TablePropertiesPB& properties_pb = response_pb.schema().table_properties();
  EXPECT_TRUE(properties_pb.has_default_time_to_live());
  // We store ttl in milliseconds internally.
  EXPECT_EQ(1000, properties_pb.default_time_to_live());
}

TEST_F(TestQLCreateTable, TestQLCreateTableWithClusteringOrderBy) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "human_resource1(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) WITH CLUSTERING ORDER BY(first_name ASC);";
  const string table2 = "human_resource2(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) WITH CLUSTERING ORDER BY(first_name ASC) AND "
      "CLUSTERING ORDER BY (last_name DESC);";
  const string table3 = "human_resource3(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(first_name ASC, last_name DESC);";
  const string table4 = "human_resource4(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(last_name ASC, last_name DESC);";
  const string table5 = "human_resource5(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(last_name ASC, first_name DESC, last_name DESC);";
  const string table6 = "human_resource6(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC, first_name DESC);";
  const string table7 = "human_resource7(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC) AND "
      "CLUSTERING ORDER BY (first_name DESC);";
  const string table8 = "human_resource8(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC, last_name DESC);";
  const string table9 = "human_resource9(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(first_name DESC, last_name DESC, something DESC);";
  const string table10 = "human_resource10(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(something ASC);";
  const string table11 = "human_resource10(id int, first_name varchar, last_name varchar, age int, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(age ASC);";
  const string table12 = "human_resource10(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(id);";
  // Create the table 1.
  EXEC_VALID_STMT(CreateStmt(table1));
  EXEC_VALID_STMT(CreateStmt(table2));
  EXEC_VALID_STMT(CreateStmt(table3));
  EXEC_VALID_STMT(CreateStmt(table4));
  EXEC_VALID_STMT(CreateStmt(table5));

  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table6),
      "Invalid Table Property. Columns in the CLUSTERING ORDER directive must be in same order "
      "as the clustering key columns order (first_name must appear before last_name)");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table7),
      "Invalid Table Property. Columns in the CLUSTERING ORDER directive must be in same order "
      "as the clustering key columns order (first_name must appear before last_name)");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table8),
      "Invalid Table Property. Missing CLUSTERING ORDER for column first_name");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table9),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table10),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table11),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table12),
      "Invalid Table Property. Not a clustering key colum");
}

TEST_F(TestQLCreateTable, TestQLCreateTableWithPartitionScemeOf) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "devices(supplier_id INT, device_id DOUBLE, model_year INT, "
      "device_name TEXT, PRIMARY KEY((supplier_id, device_id), model_year));";
  const string table2 = "descriptions1(supplier_id INT, device_id DOUBLE, description TEXT, "
      "PRIMARY KEY((supplier_id, device_id))) with partition scheme of devices;";
  const string table3 = "descriptions2(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, dev_id), image_id)) "
      "with partition scheme of devices;";
  const string table4 = "descriptions3(supplier_id INT, device_id DOUBLE, description TEXT, "
      "PRIMARY KEY(supplier_id, device_id)) with partition scheme of devices;";
  const string table5 = "descriptions4(supplier_id INT, device_id DOUBLE, model_year INT, "
      "description TEXT, PRIMARY KEY((supplier_id, device_id, model_year))) "
      "with partition scheme of devices;";
  const string table6 = "descriptions5(supplier_id INT, device_id DOUBLE, description TEXT, "
      "PRIMARY KEY((supplier_id, device_id))) with partition scheme of non_existing_table;";
  const string table7 = "descriptions6(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, description))) with partition scheme of devices;";
  const string table8 = "descriptions7(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((dev_id, supp_id))) with partition scheme of devices;";
  const string table9 = "descriptions8(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, image_id))) with partition scheme of devices;";
  const string table10 = "descriptions9(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((description, image_id))) with partition scheme of devices;";
  const string table11 = "descriptions10(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, dev_id))) with partition scheme devices;";
  const string table12 = "descriptions11(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, dev_id))) with partition schema of devices;";
  const string table13 = "descriptions12(supp_id INT, dev_id DOUBLE, description TEXT, "
      "image_id INT, PRIMARY KEY((supp_id, dev_id))) with partitioning scheme of devices;";

  // Create the devices tables.
  EXEC_VALID_STMT(CreateStmt(table1));

  EXEC_VALID_STMT(CreateStmt(table2));
  EXEC_VALID_STMT(CreateStmt(table3));
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table4),
                                 "The number of hash keys in the current table "
                                     "differ from the number of hash keys in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table5),
                                 "The number of hash keys in the current table "
                                     "differ from the number of hash keys in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table6),
                                 "Table Not Found");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table7),
                                 "The hash key 'description' in the current table has a different "
                                     "datatype from the corresponding hash key in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table8),
                                 "The hash key 'dev_id' in the current table has a different "
                                     "datatype from the corresponding hash key in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table9),
                                 "The hash key 'image_id' in the current table has a different "
                                     "datatype from the corresponding hash key in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table10),
                                 "The hash key 'description' in the current table has a different "
                                     "datatype from the corresponding hash key in 'devices'");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table11),
                                 "syntax error");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table12),
                                 "syntax error");
  EXEC_INVALID_TABLE_CREATE_STMT(CreateStmt(table13),
                                 "syntax error");
}

} // namespace ql
} // namespace yb
