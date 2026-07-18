// Copyright (c) YugabyteDB, Inc.
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

#include <regex>
#include <string>

#include <gtest/gtest.h>

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/sys_catalog.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::master {

class SysCatalogITest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(pgwrapper::PgMiniTestBase);

    master::GetNamespaceInfoResponsePB resp;
    ASSERT_OK(client_->GetNamespaceInfo(namespace_name, YQL_DATABASE_PGSQL, &resp));
    namespace_id_ = resp.namespace_().id();

    google::SetVLOGLevel("catalog_manager*", 1);
  }

  const std::string namespace_name = "yugabyte";
  NamespaceId namespace_id_;
};

TEST_F(SysCatalogITest, ReadHighestPreservableOidForNormalSpace) {
  auto conn = ASSERT_RESULT(ConnectToDB(namespace_name));
  auto database_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(namespace_id_));
  auto sys_catalog = ASSERT_RESULT(catalog_manager())->sys_catalog();

  auto ReadHighestPreservableOidForNormalSpace = [&sys_catalog, database_oid]() -> uint32_t {
    auto maximum_oids = sys_catalog->ReadHighestPreservableOids(database_oid);
    EXPECT_OK(maximum_oids);
    uint32_t oid = 0;
    if (maximum_oids) {
      oid = maximum_oids->for_normal_space_;
    }
    return oid;
  };

  auto original_highest_oid = ReadHighestPreservableOidForNormalSpace();
  // Make sure all the OIDs we are going to use are higher than any of the starting OIDs.
  ASSERT_LT(original_highest_oid, 20'000);

  // Create secondary space OIDs of each of the kinds we preserve.
  {
    // Here @ will be replaced by the lowest secondary space OID.
    std::string command = R"(
       SET yb_binary_restore = true;
       SET yb_ignore_pg_class_oids = false;
       SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid((@)::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid((@+1)::pg_catalog.oid);
       CREATE TYPE high_enum AS ENUM ();
       SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid((@+2)::pg_catalog.oid);
       SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1'::real);
       ALTER TYPE high_enum ADD VALUE 'red';
       SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid((@+3)::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode((@+4)::pg_catalog.oid);
       CREATE SEQUENCE high_sequence;
      )";
    ASSERT_OK(conn.Execute(std::regex_replace(
        command, std::regex{"@"}, std::to_string(kPgFirstSecondarySpaceObjectId))));
    auto oid = ReadHighestPreservableOidForNormalSpace();
    // The addition of these OIDs should not have changed the highest normal space OID at all.
    ASSERT_EQ(oid, original_highest_oid);
  }

  {
    // Run CREATE TYPE new_enum AS ENUM ('red', 'orange'); but using 50000-50001 as pg_enum OIDs.
    ASSERT_OK(conn.Execute(R"(
       SET yb_binary_restore = true;
       SET yb_ignore_pg_class_oids = false;
       SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('30000'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('30001'::pg_catalog.oid);

       CREATE TYPE new_enum AS ENUM ();
       SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('50000'::pg_catalog.oid);
       SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1'::real);
       ALTER TYPE new_enum ADD VALUE 'red';
       SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid('50001'::pg_catalog.oid);
       SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('2'::real);
       ALTER TYPE new_enum ADD VALUE 'orange';
      )"));
    auto oid = ReadHighestPreservableOidForNormalSpace();
    EXPECT_EQ(oid, 50'001);
  }

  {
    // Run CREATE SEQUENCE new_sequence; using pg_class OID 60000.
    ASSERT_OK(conn.Execute(R"(
       SET yb_binary_restore = true;
       SET yb_ignore_pg_class_oids = false;
       SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('60000'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('30010'::pg_catalog.oid);

       CREATE SEQUENCE new_sequence;
      )"));
    auto oid = ReadHighestPreservableOidForNormalSpace();
    EXPECT_EQ(oid, 60000);
  }

  {
    // Run CREATE TYPE new_composite AS (x int); using pg_type OID 70000-70001.
    ASSERT_OK(conn.Execute(R"(
       SET yb_binary_restore = true;
       SET yb_ignore_pg_class_oids = false;
       SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('70000'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('70001'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('30020'::pg_catalog.oid);

       CREATE TYPE new_composite AS (x int);
      )"));
    auto oid = ReadHighestPreservableOidForNormalSpace();
    EXPECT_EQ(oid, 70'001);
  }

  {
    // Run CREATE TABLE public.my_table (x integer); attempting to use pg_relfilenode 80000.
    ASSERT_OK(conn.Execute(R"(
       SET yb_binary_restore = true;
       SET yb_ignore_pg_class_oids = false;
       SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid('30030'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid('30031'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid('30032'::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode('80000'::pg_catalog.oid);

       CREATE TABLE my_table (x integer);
      )"));

    // TODO(yhaddad): fix Postgres to honor binary_upgrade_set_next_heap_relfilenode directive
    // then update this test to expect 80'000.
    //
    // (Currently this directive is not honored, which prevents this test from testing the
    // relfilenode case.)
    LOG(INFO) << ASSERT_RESULT(conn.FetchAllAsString("SELECT oid, relfilenode FROM pg_class;"));
    auto oid = ReadHighestPreservableOidForNormalSpace();
    // EXPECT_EQ(oid, 80'000);
    EXPECT_EQ(oid, 70'001);
  }
}

TEST_F(SysCatalogITest, ReadHighestPreservableOidForSecondarySpace) {
  auto conn = ASSERT_RESULT(ConnectToDB(namespace_name));
  auto database_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(namespace_id_));
  auto sys_catalog = ASSERT_RESULT(catalog_manager())->sys_catalog();

  auto ReadHighestPreservableOidForSecondarySpace = [&sys_catalog, database_oid]() -> uint32_t {
    auto maximum_oids = sys_catalog->ReadHighestPreservableOids(database_oid);
    EXPECT_OK(maximum_oids);
    uint32_t oid = 0;
    if (maximum_oids) {
      oid = maximum_oids->for_secondary_space_;
    }
    return oid;
  };

  const uint32_t base_oid = kPgFirstSecondarySpaceObjectId + 100;
  // Here @ in statement will be replaced by base_oid.
  auto MyExecute = [&conn](const std::string& statement) {
    const std::string prefix =
        "SET yb_binary_restore = true; SET yb_ignore_pg_class_oids = false; ";
    return conn.Execute(
        prefix + std::regex_replace(statement, std::regex{"@"}, std::to_string(base_oid)));
  };

  {
    auto oid = ReadHighestPreservableOidForSecondarySpace();
    EXPECT_EQ(oid, kPgFirstSecondarySpaceObjectId);
  }

  // Create secondary space OIDs of each of the kinds we preserve.

  {
    ASSERT_OK(MyExecute(R"(
       SELECT pg_catalog.binary_upgrade_set_next_pg_type_oid((@)::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_array_pg_type_oid((@+1)::pg_catalog.oid);
       CREATE TYPE high_enum AS ENUM ();
       )"));
    auto oid = ReadHighestPreservableOidForSecondarySpace();
    ASSERT_EQ(oid, base_oid + 1);
  }

  {
    ASSERT_OK(MyExecute(R"(
       SELECT pg_catalog.binary_upgrade_set_next_pg_enum_oid((@+2)::pg_catalog.oid);
       SELECT pg_catalog.yb_binary_upgrade_set_next_pg_enum_sortorder('1'::real);
       ALTER TYPE high_enum ADD VALUE 'red';
       )"));
    auto oid = ReadHighestPreservableOidForSecondarySpace();
    ASSERT_EQ(oid, base_oid + 2);
  }

  {
    ASSERT_OK(MyExecute(R"(
       SELECT pg_catalog.binary_upgrade_set_next_heap_pg_class_oid((@+3)::pg_catalog.oid);
       SELECT pg_catalog.binary_upgrade_set_next_heap_relfilenode((@+4)::pg_catalog.oid);
       CREATE SEQUENCE high_sequence;
       )"));
    auto oid = ReadHighestPreservableOidForSecondarySpace();
    // TODO(yhaddad): fix Postgres to honor binary_upgrade_set_next_heap_relfilenode directive
    // then update this test to expect base_oid + 4.
    //
    // (Currently this directive is not honored, which prevents this test from testing the
    // relfilenode case.)
    // ASSERT_EQ(oid, base_oid + 4);
    ASSERT_EQ(oid, base_oid + 3);
  }
}

}  // namespace yb::master
