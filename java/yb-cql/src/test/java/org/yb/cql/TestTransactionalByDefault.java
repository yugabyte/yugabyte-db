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
package org.yb.cql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestTransactionalByDefault extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestTransactionalByDefault.class);

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("cql_table_is_transactional_by_default", "true");
  }

  @Test
  public void testCreateTableAndIndex() throws Exception {
    LOG.info("Start test: " + getCurrentTestMethodName());

    // Create transactional test tables and indexes.
    session.execute("create table test_tr_tbl (h1 int primary key, c1 int) " +
                    "with transactions = {'enabled' : true};");
    session.execute("create index on test_tr_tbl(c1);");
    session.execute("create index idx2 on test_tr_tbl(c1) with " +
                    "transactions = {'enabled' : true};");

    runInvalidStmt("create index on test_tr_tbl(c1) with " +
                   "transactions = {'enabled' : false};");
    runInvalidStmt("create index on test_tr_tbl(c1) with " +
                   "transactions = {'enabled' : true, 'consistency_level' : 'user_enforced'};");
    runInvalidStmt("create index on test_tr_tbl(c1) with " +
                   "transactions = {'enabled' : false, 'consistency_level' : 'user_enforced'};");
    runInvalidStmt("create index on test_tr_tbl(c1) with " +
                   "transactions = {'consistency_level' : 'user_enforced'};");

    session.execute("create table test_reg_tbl (h1 int primary key, c1 int);");
    session.execute("create index on test_reg_tbl(c1);");
    session.execute("create index idx3 on test_reg_tbl(c1) with " +
                    "transactions = {'enabled' : true};");

    runInvalidStmt("create index on test_reg_tbl(c1) with " +
                   "transactions = {'enabled' : false};");
    runInvalidStmt("create index on test_reg_tbl(c1) with " +
                   "transactions = {'enabled' : true, 'consistency_level' : 'user_enforced'};");
    runInvalidStmt("create index on test_reg_tbl(c1) with " +
                   "transactions = {'enabled' : false, 'consistency_level' : 'user_enforced'};");
    runInvalidStmt("create index on test_reg_tbl(c1) with " +
                   "transactions = {'consistency_level' : 'user_enforced'};");

    // Create non-transactional test table and index.
    session.execute("create table test_non_tr_tbl (h1 int primary key, c1 int) " +
                    "with transactions = {'enabled' : false};");

    runInvalidStmt("create index on test_non_tr_tbl(c1);");
    runInvalidStmt("create index on test_non_tr_tbl(c1) with " +
                   "transactions = {'enabled' : true};");
    runInvalidStmt("create index on test_non_tr_tbl(c1) with " +
                   "transactions = {'enabled' : false};");
    runInvalidStmt("create index on test_non_tr_tbl(c1) with " +
                   "transactions = {'enabled' : true, 'consistency_level' : 'user_enforced'};");
    runInvalidStmt("create index on test_non_tr_tbl(c1) with " +
                   "transactions = {'consistency_level' : 'user_enforced'};");

    // Test weak index.
    session.execute("create index test_non_tr_tbl_idx on test_non_tr_tbl(c1) with " +
                    "transactions = {'enabled' : false, 'consistency_level' : 'user_enforced'};");
    assertQuery("select options, transactions from system_schema.indexes where " +
                "index_name = 'test_non_tr_tbl_idx';",
                "Row[{target=c1, h1}, {enabled=false, consistency_level=user_enforced}]");

    // Drop test tables.
    session.execute("drop table test_tr_tbl;");
    session.execute("drop table test_non_tr_tbl;");
    session.execute("drop table test_reg_tbl;");

    LOG.info("End test: " + getCurrentTestMethodName());
  }
}
