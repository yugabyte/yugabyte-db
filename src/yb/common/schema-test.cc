// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#include <unordered_map>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/common/common.pb.h"
#include "yb/common/row.h"
#include "yb/common/schema.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace tablet {

using std::vector;

// Test basic functionality of Schema definition
TEST(TestSchema, TestSchema) {
  Schema empty_schema;
  ASSERT_GT(empty_schema.memory_footprint_excluding_this(), 0);

  ColumnSchema col1("key", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST);
  ColumnSchema col2("uint32val", DataType::UINT32, ColumnKind::VALUE, Nullable::kTrue);
  ColumnSchema col3("int32val", DataType::INT32);

  vector<ColumnSchema> cols = { col1, col2, col3 };
  Schema schema(cols);

  ASSERT_EQ(sizeof(Slice) + sizeof(uint32_t) + sizeof(int32_t),
            schema.byte_size());
  ASSERT_EQ(3, schema.num_columns());
  ASSERT_EQ(0, schema.column_offset(0));
  ASSERT_EQ(sizeof(Slice), schema.column_offset(1));
  ASSERT_GT(schema.memory_footprint_excluding_this(),
            empty_schema.memory_footprint_excluding_this());

  EXPECT_EQ(Format("Schema [\n"
                   "\tkey[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\tuint32val[uint32 NULLABLE VALUE],\n"
                   "\tint32val[int32 NOT NULL VALUE]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $0",
                   kCurrentPartitioningVersion),
            schema.ToString());
  EXPECT_EQ("key[string NOT NULL RANGE_ASC_NULL_FIRST]", schema.column(0).ToString());
  EXPECT_EQ("uint32 NULLABLE VALUE", schema.column(1).TypeToString());
}

TEST(TestSchema, TestReset) {
  Schema schema;
  ASSERT_FALSE(schema.initialized());

  ASSERT_OK(schema.Reset({ ColumnSchema("col3", DataType::UINT32, ColumnKind::RANGE_ASC_NULL_FIRST),
                           ColumnSchema("col2", DataType::STRING) }));
  ASSERT_TRUE(schema.initialized());
}

// Test for KUDU-943, a bug where we suspected that Variant didn't behave
// correctly with empty strings.
TEST(TestSchema, TestEmptyVariant) {
  Slice empty_val("");
  Slice nonempty_val("test");

  Variant v(DataType::STRING, &nonempty_val);
  ASSERT_EQ("test", (static_cast<const Slice*>(v.value()))->ToString());
  v.Reset(DataType::STRING, &empty_val);
  ASSERT_EQ("", (static_cast<const Slice*>(v.value()))->ToString());
  v.Reset(DataType::STRING, &nonempty_val);
  ASSERT_EQ("test", (static_cast<const Slice*>(v.value()))->ToString());
}

TEST(TestSchema, TestProjectSubset) {
  Schema schema1({
      ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
      ColumnSchema("col2", DataType::STRING),
      ColumnSchema("col3", DataType::UINT32) });

  Schema schema2({ ColumnSchema("col3", DataType::UINT32),
                   ColumnSchema("col2", DataType::STRING) });

  RowProjector row_projector(&schema1, &schema2);
  ASSERT_OK(row_projector.Init());

  // Verify the mapping
  ASSERT_EQ(2, row_projector.base_cols_mapping().size());
  ASSERT_EQ(0, row_projector.adapter_cols_mapping().size());

  const vector<RowProjector::ProjectionIdxMapping>& mapping = row_projector.base_cols_mapping();
  ASSERT_EQ(mapping[0].first, 0);  // col3 schema2
  ASSERT_EQ(mapping[0].second, 2); // col3 schema1
  ASSERT_EQ(mapping[1].first, 1);  // col2 schema2
  ASSERT_EQ(mapping[1].second, 1); // col2 schema1
}

// Test projection when the type of the projected column
// doesn't match the original type.
TEST(TestSchema, TestProjectTypeMismatch) {
  Schema schema1({ ColumnSchema("key", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("val", DataType::UINT32) });
  Schema schema2({ ColumnSchema("val", DataType::STRING) });

  RowProjector row_projector(&schema1, &schema2);
  Status s = row_projector.Init();
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.message().ToString(), "must have type");
}

// Test projection when the some columns in the projection
// are not present in the base schema
TEST(TestSchema, TestProjectMissingColumn) {
  Schema schema1({ ColumnSchema("key", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("val", DataType::UINT32) });
  Schema schema2({ ColumnSchema("val", DataType::UINT32),
                   ColumnSchema("non_present", DataType::STRING) });
  Schema schema3({ ColumnSchema("val", DataType::UINT32),
                   ColumnSchema("non_present", DataType::UINT32, ColumnKind::VALUE,
                                Nullable::kTrue) });

  RowProjector row_projector(&schema1, &schema2);
  Status s = row_projector.Init();
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.message().ToString(),
    "does not exist in the projection, and it does not have a nullable type");

  // Verify Default nullable column with no default value
  ASSERT_OK(row_projector.Reset(&schema1, &schema3));

  ASSERT_EQ(1, row_projector.base_cols_mapping().size());
  ASSERT_EQ(0, row_projector.adapter_cols_mapping().size());

  ASSERT_EQ(row_projector.base_cols_mapping()[0].first, 0);  // val schema2
  ASSERT_EQ(row_projector.base_cols_mapping()[0].second, 1); // val schema1
}

// Test projection mapping using IDs.
// This simulate a column rename ('val' -> 'val_renamed')
// and a new column added ('non_present')
TEST(TestSchema, TestProjectRename) {
  SchemaBuilder builder;
  ASSERT_OK(builder.AddKeyColumn("key", DataType::STRING));
  ASSERT_OK(builder.AddColumn("val", DataType::UINT32));
  Schema schema1 = builder.Build();

  builder.Reset(schema1);
  ASSERT_OK(builder.AddNullableColumn("non_present", DataType::UINT32));
  ASSERT_OK(builder.RenameColumn("val", "val_renamed"));
  Schema schema2 = builder.Build();

  RowProjector row_projector(&schema1, &schema2);
  ASSERT_OK(row_projector.Init());

  ASSERT_EQ(2, row_projector.base_cols_mapping().size());
  ASSERT_EQ(0, row_projector.adapter_cols_mapping().size());

  ASSERT_EQ(row_projector.base_cols_mapping()[0].first, 0);  // key schema2
  ASSERT_EQ(row_projector.base_cols_mapping()[0].second, 0); // key schema1

  ASSERT_EQ(row_projector.base_cols_mapping()[1].first, 1);  // val_renamed schema2
  ASSERT_EQ(row_projector.base_cols_mapping()[1].second, 1); // val schema1
}

TEST(TestSchema, TestCreateProjection) {
  Schema schema({ ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                  ColumnSchema("col2", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                  ColumnSchema("col3", DataType::STRING),
                  ColumnSchema("col4", DataType::STRING),
                  ColumnSchema("col5", DataType::STRING) });
  Schema schema_with_ids = SchemaBuilder(schema).Build();
  Schema partial_schema;

  // By names, without IDs
  ASSERT_OK(schema.TEST_CreateProjectionByNames({ "col1", "col2", "col4" }, &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\tcol1[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\tcol2[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\tcol4[string NOT NULL VALUE]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $0",
                   kCurrentPartitioningVersion),
            partial_schema.ToString());

  // By names, with IDS
  ASSERT_OK(schema_with_ids.TEST_CreateProjectionByNames(
      { "col1", "col2", "col4" }, &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\t$0:col1[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\t$1:col2[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\t$2:col4[string NOT NULL VALUE]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $3",
                   schema_with_ids.column_id(0),
                   schema_with_ids.column_id(1),
                   schema_with_ids.column_id(3),
                   kCurrentPartitioningVersion),
            partial_schema.ToString());

  // By names, with missing names.
  Status s = schema.TEST_CreateProjectionByNames({ "foobar" }, &partial_schema);
  EXPECT_EQ("Not found: Column not found: foobar", s.ToString(/* no file/line */ false));

  // By IDs
  ASSERT_OK(schema_with_ids.CreateProjectionByIdsIgnoreMissing({ schema_with_ids.column_id(0),
                                                                 schema_with_ids.column_id(1),
                                                                 ColumnId(1000), // missing column
                                                                 schema_with_ids.column_id(3) },
                                                               &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\t$0:col1[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\t$1:col2[string NOT NULL RANGE_ASC_NULL_FIRST],\n"
                   "\t$2:col4[string NOT NULL VALUE]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $3",
                   schema_with_ids.column_id(0),
                   schema_with_ids.column_id(1),
                   schema_with_ids.column_id(3),
                   kCurrentPartitioningVersion),
            partial_schema.ToString());
}

TEST(TestSchema, TestCopyFrom) {
  TableProperties properties;
  properties.SetDefaultTimeToLive(1000);
  Schema schema1({ ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("col2", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("col3", DataType::UINT32) }, properties);
  Schema schema2;
  schema2.CopyFrom(schema1);
  ASSERT_EQ(3, schema2.num_columns());
  ASSERT_EQ(2, schema2.num_key_columns());
  ASSERT_EQ(1000, schema2.table_properties().DefaultTimeToLive());
}

TEST(TestSchema, TestSchemaBuilder) {
  TableProperties properties;
  properties.SetDefaultTimeToLive(1000);
  Schema schema1({ ColumnSchema("col1", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("col2", DataType::STRING, ColumnKind::RANGE_ASC_NULL_FIRST),
                   ColumnSchema("col3", DataType::UINT32) }, properties);
  SchemaBuilder builder(schema1);
  Schema schema2 = builder.Build();
  ASSERT_TRUE(schema1.Equals(schema2));
}

TEST(TestSchema, TestTableProperties) {
  TableProperties properties;
  ASSERT_FALSE(properties.HasDefaultTimeToLive());

  properties.SetDefaultTimeToLive(1000);
  ASSERT_TRUE(properties.HasDefaultTimeToLive());
  ASSERT_EQ(1000, properties.DefaultTimeToLive());

  TableProperties properties1;
  properties1.SetDefaultTimeToLive(1000);
  ASSERT_EQ(properties, properties1);

  TablePropertiesPB pb;
  properties.ToTablePropertiesPB(&pb);
  ASSERT_TRUE(pb.has_default_time_to_live());
  ASSERT_EQ(1000, pb.default_time_to_live());

  auto properties2 = TableProperties::FromTablePropertiesPB(pb);
  ASSERT_TRUE(properties2.HasDefaultTimeToLive());
  ASSERT_EQ(1000, properties2.DefaultTimeToLive());

  properties.Reset();
  pb.Clear();
  ASSERT_FALSE(properties.HasDefaultTimeToLive());
  properties.ToTablePropertiesPB(&pb);
  ASSERT_FALSE(pb.has_default_time_to_live());
  auto properties3 = TableProperties::FromTablePropertiesPB(pb);
  ASSERT_FALSE(properties3.HasDefaultTimeToLive());
}

} // namespace tablet
} // namespace yb
