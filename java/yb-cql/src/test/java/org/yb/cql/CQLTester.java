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
 */
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;
import com.datastax.driver.core.ColumnDefinitions;
import com.datastax.driver.core.ColumnDefinitions.Definition;
import com.datastax.driver.core.DataType;
import com.datastax.driver.core.DataType.Name;

import org.junit.Test;
import org.yb.client.TestUtils;

import java.util.List;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

/**
 * Base class for CQL tests.
 */
public class CQLTester extends BaseCQLTest
{
    public static final String KEYSPACE = "cql_test_keyspace";
    private static final AtomicInteger seqNumber = new AtomicInteger();

    private List<String> tables = new ArrayList<>();

    protected void createTable(String query)
    {
        createTable(KEYSPACE, query);
    }

    protected void createTable(String keyspace, String query)
    {
        String currentTable = createTableName();
        String fullQuery = formatQuery(keyspace, query);
        final long startTimeMs = System.currentTimeMillis();
        session.execute(fullQuery);
        long elapsedTimeMs = System.currentTimeMillis() - startTimeMs;
        LOG.info("After execute: {} took {}ms", fullQuery, elapsedTimeMs);
    }

    protected String createTableName()
    {
        String currentTable = "table_" + seqNumber.getAndIncrement();
        tables.add(currentTable);
        return currentTable;
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

    public static Object[] row(Object... expected)
    {
        return expected;
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

            assertEquals(String.format("Invalid number of (expected) values provided for row %d", i), expected == null ? 1 : expected.length, meta.size());

            for (int j = 0; j < meta.size(); j++)
            {
                ColumnDefinitions.Definition column = meta.get(j);
                DataType.Name type = column.getType().getName();
                switch (type) {
                    case INT:
                        LOG.info("Row[{}, {}] = {}", i, j, actual.getInt(j));
                        assertEquals((Integer)expected[j], (Integer)actual.getInt(j));
                        break;

                    case VARCHAR:
                    case TEXT:
                        LOG.info("Row[{}, {}] = {}", i, j, actual.getString(j));
                        assertEquals((String)expected[j], actual.getString(j));
                        break;

                    default:
                        fail(String.format("Expect INT or VARCHAR or TEXT type but got: %s", type.toString()));
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
