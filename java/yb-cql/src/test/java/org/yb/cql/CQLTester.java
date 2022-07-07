/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * The following only applies to changes made to this file as part of YugaByte development.
 *
 *     Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the
 * License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied.  See the License for the specific language governing permissions
 * and limitations under the License.
 */
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.ColumnDefinitions.Definition;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.DataType.Name;

import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.client.TestUtils;

import java.util.List;
import java.util.Set;
import java.util.HashSet;
import java.util.Arrays;
import java.util.Map;
import java.util.LinkedHashMap;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;

import com.google.common.collect.ImmutableSet;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.fail;

/**
* Base class for CQL tests.
*/
public class CQLTester extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(CQLTester.class);

  public static final String KEYSPACE = "cql_test_keyspace";
  private static final AtomicInteger seqNumber = new AtomicInteger();

  private List<String> tables = new ArrayList<>();
  private List<String> indexes = new ArrayList<>();

  protected void createTable(String query)
  {
    createTable(KEYSPACE, query);
  }

  protected void createIndex(String query)
  {
    createIndex(KEYSPACE, query);
  }

  protected void createTable(String keyspace, String query)
  {
    String currentTable = createTableName();
    String fullQuery = formatQuery(keyspace, query);
    final long startTimeMs = System.currentTimeMillis();
    session.execute(fullQuery);
    long elapsedTimeMs = System.currentTimeMillis() - startTimeMs;
    LOG.info("After createTable execute: {} took {}ms", fullQuery, elapsedTimeMs);
  }

  protected void createIndex(String keyspace, String query)
  {
    String fullQuery = String.format(query, createIndexName(), keyspace + "." + currentTable());
    final long startTimeMs = System.currentTimeMillis();
    session.execute(fullQuery);
    long elapsedTimeMs = System.currentTimeMillis() - startTimeMs;
    LOG.info("After createIndex execute: {} took {}ms", fullQuery, elapsedTimeMs);
  }

  protected String createTableName()
  {
    String currentTable = "table_" + seqNumber.getAndIncrement();
    tables.add(currentTable);
    return currentTable;
  }

  protected String createIndexName()
  {
    String currentIndex = "index_" + seqNumber.getAndIncrement();
    indexes.add(currentIndex);
    return currentIndex;
  }

  protected String formatQuery(String query)
  {
    return formatQuery(KEYSPACE, query);
  }

  protected final String formatQuery(String keyspace, String query)
  {
    String currentTable = currentTable();
    return currentTable == null ? query : String.format(query, keyspace + "." + currentTable);
  }

  protected String keyspace()
  {
    return KEYSPACE;
  }

  protected String currentTable()
  {
    if (tables.isEmpty())
      return null;
    return tables.get(tables.size() - 1);
  }

  public ResultSet execute(String query, Object... values) throws Exception {
    String fullQuery = formatQuery(KEYSPACE, query);
    final ResultSet result = session.execute(fullQuery, values);
    LOG.info("EXEC CQL FINISHED: {}", fullQuery);
    return result;
  }


  public ResultSet execute(String query) throws Exception {
    String fullQuery = formatQuery(KEYSPACE, query);
    final ResultSet result = session.execute(fullQuery);
    LOG.info("EXEC CQL FINISHED: {}", fullQuery);
    return result;
  }

  protected void assertAllRows(Object[]... rows) throws Throwable
  {
    assertRows(execute("SELECT * FROM %s"), rows);
  }

  protected void assertEmpty(ResultSet result) throws Throwable
  {
    if (result != null && !result.isExhausted())
    {
      List<String> rows = makeRowStrings(result);
      throw new AssertionError(String.format("Expected empty result but got %d rows: %s \n",
                                             rows.size(), rows));
    }
  }

  public static Object[] row(Object... expected)
  {
    return expected;
  }

  protected Object list(Object...values)
  {
    return Arrays.asList(values);
  }

  protected Object set(Object...values)
  {
    return ImmutableSet.copyOf(values);
  }

  protected Object map(Object...values)
  {
    if (values.length % 2 != 0)
      throw new IllegalArgumentException("Invalid number of arguments, got " + values.length);

    int size = values.length / 2;
    Map m = new LinkedHashMap(size);
    for (int i = 0; i < size; i++)
      m.put(values[2 * i], values[(2 * i) + 1]);
    return m;
  }

  protected static List<String> makeRowStrings(ResultSet resultSet)
  {
    List<String> rows = new ArrayList<>();
    int i = 0;
    for (Row row : resultSet)
    {
      rows.add(String.format("Row {} = {}", i, row.toString()));
      i++;
    }

    return rows;
  }

  public static int displayRows(ResultSet result)
  {
    if (result == null)
    {
      LOG.info("Resultset has zero rows");
      return 0;
    }

    ColumnDefinitions colDef = result.getColumnDefinitions();
    List<ColumnDefinitions.Definition> meta = colDef.asList();
    int i = 0;
    for (Row actual : result)
    {
      LOG.info("Row {} = {}", i, actual.toString());
      i++;
    }
    return i;
  }

  public static void assertRows(ResultSet result, Object[]... rows)
  {
    if (result == null)
    {
      if (rows.length > 0)
        fail(String.format("No rows returned by query but %d expected", rows.length));
      return;
    }
    ColumnDefinitions colDef = result.getColumnDefinitions();
    List<ColumnDefinitions.Definition> meta = colDef.asList();
    Iterator<Row> iter = result.iterator();
    int i = 0;
    while (iter.hasNext() && i < rows.length)
    {
      Object[] expected = rows[i];
      Row actual = iter.next();

      assertEquals(String.format("Invalid number of (expected) values provided for row %d", i),
                   expected == null ? 1 : expected.length, meta.size());

      for (int j = 0; j < meta.size(); j++)
      {
        ColumnDefinitions.Definition column = meta.get(j);
        DataType type = column.getType();
        DataType.Name typeName = type.getName();
        switch (typeName)
        {
          case INT:
            Integer intActual = actual.isNull(j) ? null : actual.getInt(j);
            LOG.info("Row[{}, {}] = {}", i, j, intActual);
            assertEquals((Integer)expected[j], intActual);
            break;

          case VARCHAR:
          case TEXT:
            LOG.info("Row[{}, {}] = {}", i, j, actual.getString(j));
            assertEquals((String)expected[j], actual.getString(j));
            break;

          case SET:
            List<DataType> setType = type.getTypeArguments();
            DataType elementType = setType.get(0);
            String setValue;
            switch (elementType.getName())
            {
              case INT:
                Set<Integer> intSet = actual.getSet(j, Integer.class);
                ImmutableSet<Integer> expIntSet = (ImmutableSet<Integer>)expected[j];
                setValue = intSet.stream().map(String::valueOf).collect(Collectors.joining(","));
                LOG.info("Row[{}, {}] = {}", i, j, setValue);
                assertTrue(intSet.equals(expIntSet));
                break;

              case VARCHAR:
              case TEXT:
                Set<String> strSet = actual.getSet(j, String.class);
                ImmutableSet<String> expStrSet = (ImmutableSet<String>)expected[j];
                setValue = strSet.stream().map(String::valueOf).collect(Collectors.joining(","));
                LOG.info("Row[{}, {}] = {}", i, j, setValue);
                assertTrue(strSet.equals(expStrSet));
                break;

              default:
                fail(String.format("Expect INT or VARCHAR or TEXT element type of Set but got: %s",
                                   type.toString()));
                break;
            }
            break;

          default:
            fail(String.format("Expect INT or VARCHAR or TEXT or SET type but got: %s",
                               type.toString()));
            break;
        }
      }
      i++;
    }

    if (iter.hasNext())
      fail(String.format("Query returned more than %d rows expected", rows.length));
    if (i != rows.length)
      fail(String.format("Query returned %d rows but expected %d rows", i, rows.length));
  }
}
