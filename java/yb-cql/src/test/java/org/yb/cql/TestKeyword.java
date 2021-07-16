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

import java.util.*;
import java.util.stream.Collectors;
import java.text.SimpleDateFormat;

import com.datastax.driver.core.LocalDate;
import com.datastax.driver.core.SimpleStatement;
import com.datastax.driver.core.Statement;
import com.yugabyte.driver.core.TableSplitMetadata;
import com.yugabyte.driver.core.policies.PartitionAwarePolicy;
import org.junit.Test;

import org.yb.minicluster.IOMetrics;
import org.yb.minicluster.Metrics;
import org.yb.minicluster.MiniYBCluster;
import org.yb.minicluster.MiniYBDaemon;
import org.yb.minicluster.RocksDBMetrics;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.exceptions.InvalidQueryException;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertNull;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestKeyword extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestKeyword.class);

  private final String[] unreserved_keywords = {
    "group",
    "offset",
    "user",
    "when"
  };

  @Test
  public void testUnreservedKeyword() throws Exception {
    LOG.info("TEST CQL UNRESERVED_KEYWORD - Start");

    // Construct CQL statements using unreserved_keywords.
    String create_stmt = "CREATE TABLE tab (pk INT PRIMARY KEY";
    String insert_stmt = "INSERT INTO tab (pk";
    String value_clause = "VALUES (1";
    String select_stmt = "SELECT pk";
    String select_result = "Row[1";
    for (String unreserved_keyword : unreserved_keywords) {
      create_stmt += ", " + unreserved_keyword + " INT";
      insert_stmt += ", " + unreserved_keyword;
      value_clause += ", 1";
      select_stmt += ", " + unreserved_keyword;
      select_result += ", 1";
    }
    create_stmt += ");";
    value_clause += ");";
    insert_stmt += ") " + value_clause;
    select_stmt += " FROM tab;";
    select_result += "]";

    // Create table whose column names are unreserved keyword.
    execute(create_stmt);

    // INSERT data INTO columns whose names are unreserved keyword.
    execute(insert_stmt);

    // SELECT data FROM columns whose names are unreserved keyword.
    LOG.info("SELECT stmt: " + select_stmt);
    LOG.info("SELECT expected result: " + select_result);
    assertQuery(select_stmt, select_result);

    LOG.info("TEST CQL UNRESERVED_KEYWORD - Done");
  }
}
