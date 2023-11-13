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

  ColumnSchema col1("key", STRING);
  ColumnSchema col2("uint32val", UINT32, true);
  ColumnSchema col3("int32val", INT32);

  vector<ColumnSchema> cols = { col1, col2, col3 };
  Schema schema(cols, 1);

  ASSERT_EQ(sizeof(Slice) + sizeof(uint32_t) + sizeof(int32_t),
            schema.byte_size());
  ASSERT_EQ(3, schema.num_columns());
  ASSERT_EQ(0, schema.column_offset(0));
  ASSERT_EQ(sizeof(Slice), schema.column_offset(1));
  ASSERT_GT(schema.memory_footprint_excluding_this(),
            empty_schema.memory_footprint_excluding_this());

  EXPECT_EQ(Format("Schema [\n"
                   "\tkey[string NOT NULL NOT A PARTITION KEY],\n"
                   "\tuint32val[uint32 NULLABLE NOT A PARTITION KEY],\n"
                   "\tint32val[int32 NOT NULL NOT A PARTITION KEY]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $0",
                   kCurrentPartitioningVersion),
            schema.ToString());
  EXPECT_EQ("key[string NOT NULL NOT A PARTITION KEY]", schema.column(0).ToString());
  EXPECT_EQ("uint32 NULLABLE NOT A PARTITION KEY", schema.column(1).TypeToString());
  EXPECT_FALSE(schema.doc_key_offsets().has_value());
}

TEST(TestSchema, TestSwap) {
  TableProperties properties1;
  properties1.SetDefaultTimeToLive(1000);
  Schema schema1({ ColumnSchema("col1", STRING),
                   ColumnSchema("col2", STRING),
                   ColumnSchema("col3", UINT32) },
                 2, properties1);
  ASSERT_FALSE(schema1.doc_key_offsets().has_value());
  TableProperties properties2;
  properties2.SetDefaultTimeToLive(2000);
  Schema schema2({ ColumnSchema("col3", UINT32),
                   ColumnSchema("col2", STRING) },
                 1, properties2);
  schema1.swap(schema2);
  ASSERT_EQ(2, schema1.num_columns());
  ASSERT_EQ(1, schema1.num_key_columns());
  ASSERT_EQ(3, schema2.num_columns());
  ASSERT_EQ(2, schema2.num_key_columns());
  ASSERT_EQ(2000, schema1.table_properties().DefaultTimeToLive());
  ASSERT_EQ(1000, schema2.table_properties().DefaultTimeToLive());
  ASSERT_TRUE(schema1.doc_key_offsets().has_value());
  ASSERT_FALSE(schema2.doc_key_offsets().has_value());
}

TEST(TestSchema, TestReset) {
  Schema schema;
  ASSERT_FALSE(schema.initialized());

  ASSERT_OK(schema.Reset({ ColumnSchema("col3", UINT32),
                           ColumnSchema("col2", STRING) },
                         1));
  ASSERT_TRUE(schema.initialized());

  // Swap the initialized schema with an uninitialized one.
  Schema schema2;
  schema2.swap(schema);
  ASSERT_FALSE(schema.initialized());
  ASSERT_TRUE(schema2.initialized());
}

// Test for KUDU-943, a bug where we suspected that Variant didn't behave
// correctly with empty strings.
TEST(TestSchema, TestEmptyVariant) {
  Slice empty_val("");
  Slice nonempty_val("test");

  Variant v(STRING, &nonempty_val);
  ASSERT_EQ("test", (static_cast<const Slice*>(v.value()))->ToString());
  v.Reset(STRING, &empty_val);
  ASSERT_EQ("", (static_cast<const Slice*>(v.value()))->ToString());
  v.Reset(STRING, &nonempty_val);
  ASSERT_EQ("test", (static_cast<const Slice*>(v.value()))->ToString());
}

TEST(TestSchema, TestProjectSubset) {
  Schema schema1({ ColumnSchema("col1", STRING),
                   ColumnSchema("col2", STRING),
                   ColumnSchema("col3", UINT32) },
                 1);

  Schema schema2({ ColumnSchema("col3", UINT32),
                   ColumnSchema("col2", STRING) },
                 0);

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
  Schema schema1({ ColumnSchema("key", STRING),
                   ColumnSchema("val", UINT32) },
                 1);
  Schema schema2({ ColumnSchema("val", STRING) }, 0);

  RowProjector row_projector(&schema1, &schema2);
  Status s = row_projector.Init();
  ASSERT_TRUE(s.IsInvalidArgument());
  ASSERT_STR_CONTAINS(s.message().ToString(), "must have type");
}

// Test projection when the some columns in the projection
// are not present in the base schema
TEST(TestSchema, TestProjectMissingColumn) {
  Schema schema1({ ColumnSchema("key", STRING), ColumnSchema("val", UINT32) }, 1);
  Schema schema2({ ColumnSchema("val", UINT32), ColumnSchema("non_present", STRING) }, 0);
  Schema schema3({ ColumnSchema("val", UINT32), ColumnSchema("non_present", UINT32, true) }, 0);

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
  ASSERT_OK(builder.AddKeyColumn("key", STRING));
  ASSERT_OK(builder.AddColumn("val", UINT32));
  Schema schema1 = builder.Build();

  builder.Reset(schema1);
  ASSERT_OK(builder.AddNullableColumn("non_present", UINT32));
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
  Schema schema({ ColumnSchema("col1", STRING),
                  ColumnSchema("col2", STRING),
                  ColumnSchema("col3", STRING),
                  ColumnSchema("col4", STRING),
                  ColumnSchema("col5", STRING) },
                2);
  Schema schema_with_ids = SchemaBuilder(schema).Build();
  Schema partial_schema;

  // By names, without IDs
  ASSERT_OK(schema.CreateProjectionByNames({ "col1", "col2", "col4" }, &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\tcol1[string NOT NULL NOT A PARTITION KEY],\n"
                   "\tcol2[string NOT NULL NOT A PARTITION KEY],\n"
                   "\tcol4[string NOT NULL NOT A PARTITION KEY]\n"
                   "]\nproperties: contain_counters: false is_transactional: false "
                   "consistency_level: STRONG "
                   "use_mangled_column_name: false "
                   "is_ysql_catalog_table: false "
                   "retain_delete_markers: false "
                   "partitioning_version: $0",
                   kCurrentPartitioningVersion),
            partial_schema.ToString());

  // By names, with IDS
  ASSERT_OK(schema_with_ids.CreateProjectionByNames({ "col1", "col2", "col4" }, &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\t$0:col1[string NOT NULL NOT A PARTITION KEY],\n"
                   "\t$1:col2[string NOT NULL NOT A PARTITION KEY],\n"
                   "\t$2:col4[string NOT NULL NOT A PARTITION KEY]\n"
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
  Status s = schema.CreateProjectionByNames({ "foobar" }, &partial_schema);
  EXPECT_EQ("Not found: Column not found: foobar", s.ToString(/* no file/line */ false));

  // By IDs
  ASSERT_OK(schema_with_ids.CreateProjectionByIdsIgnoreMissing({ schema_with_ids.column_id(0),
                                                                 schema_with_ids.column_id(1),
                                                                 ColumnId(1000), // missing column
                                                                 schema_with_ids.column_id(3) },
                                                               &partial_schema));
  EXPECT_EQ(Format("Schema [\n"
                   "\t$0:col1[string NOT NULL NOT A PARTITION KEY],\n"
                   "\t$1:col2[string NOT NULL NOT A PARTITION KEY],\n"
                   "\t$2:col4[string NOT NULL NOT A PARTITION KEY]\n"
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
  Schema schema1({ ColumnSchema("col1", STRING),
                     ColumnSchema("col2", STRING),
                     ColumnSchema("col3", UINT32) },
                 2, properties);
  Schema schema2;
  schema2.CopyFrom(schema1);
  ASSERT_EQ(3, schema2.num_columns());
  ASSERT_EQ(2, schema2.num_key_columns());
  ASSERT_EQ(1000, schema2.table_properties().DefaultTimeToLive());
}

TEST(TestSchema, TestSchemaBuilder) {
  TableProperties properties;
  properties.SetDefaultTimeToLive(1000);
  Schema schema1({ ColumnSchema("col1", STRING),
                     ColumnSchema("col2", STRING),
                     ColumnSchema("col3", UINT32) },
                 2, properties);
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

void TestKeyColDocKeyOffsets(size_t hash_key_cols) {
  constexpr size_t key_cols = 2;
  LOG(INFO) << "Testing " << hash_key_cols << " hash key cols out of " << key_cols;
  ASSERT_LE(hash_key_cols, key_cols);

  size_t key_idx = 0;
  ColumnSchema col1(
      "key_1", INT32, /* is_nullable = */ false, /* is_hashkey = */ key_idx++ < hash_key_cols);
  ColumnSchema col2(
      "key_2", UINT32, /* is_nullable = */ false, /* is_hashkey = */ key_idx++ < hash_key_cols);
  ColumnSchema col3("int32val", INT32);

  vector<ColumnSchema> cols = {col1, col2, col3};
  Schema schema(cols, key_cols);

  ASSERT_EQ(cols.size(), schema.num_columns());
  ASSERT_EQ(key_cols, schema.num_key_columns());
  ASSERT_EQ(hash_key_cols, schema.num_hash_key_columns());
  ASSERT_EQ(key_cols - hash_key_cols, schema.num_range_key_columns());

  // Note: we only validate tracking values in this test.
  // Offset validation is performed in DocKey test.
  const auto& doc_key_offsets = schema.doc_key_offsets();
  ASSERT_TRUE(doc_key_offsets.has_value());
  ASSERT_GT(doc_key_offsets->doc_key_size, 0);
  ASSERT_GE(doc_key_offsets->hash_part_size, 0);
  size_t last_col_offset = 0;
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    const auto& cur_col_offset = doc_key_offsets->key_offsets[i];
    ASSERT_LT(cur_col_offset, doc_key_offsets->doc_key_size);
    if (i < hash_key_cols) {
      ASSERT_LE(cur_col_offset, doc_key_offsets->hash_part_size);
    } else {
      ASSERT_GE(cur_col_offset, doc_key_offsets->hash_part_size);
    }
    ASSERT_GE(cur_col_offset, last_col_offset) << "Key offset: " << i;
    last_col_offset = cur_col_offset;
  }
}

TEST(TestSchema, KeyColDocKeyOffsets) {
  TestKeyColDocKeyOffsets(/* hash_key_cols = */ 2);
  TestKeyColDocKeyOffsets(/* hash_key_cols = */ 0);
  TestKeyColDocKeyOffsets(/* hash_key_cols = */ 1);
}

} // namespace tablet
} // namespace yb
