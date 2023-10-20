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

package org.yb.cdc;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.client.*;
import org.yb.YBTestRunner;


import java.util.ArrayList;
import java.util.List;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class TestCDCStream extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestCDCStream.class);

  private final String DEFAULT_NAMESPACE = "yugabyte";

  @Before
  public void setUp() throws Exception {
    super.setUp();
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
  }

  /**
   * Test to verify creation of a CDC stream when correct parameters are provided
   */
  @Test
  public void testStreamCreationWithCorrectParams() {
    try {
      statement.execute("create table test (a int primary key, b int, c numeric);");

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      YBClient myClient = testSubscriber.getSyncClient();
      assertNotNull(myClient);

      // The below table would only be for the table yugabyte.test
      YBTable table = testSubscriber.getTable();
      assertNotNull(table);

      CreateCDCStreamResponse resp =
        myClient.createCDCStream(table, DEFAULT_NAMESPACE, "proto", "implicit");

      assertNotNull(resp);
      assertFalse(resp.getStreamId().isEmpty());
    } catch (Exception e) {
      LOG.error("Test to verify correct stream creation failed", e);
      fail();
    }
  }

  /**
   * Negative test: Trying to create a CDC stream on a table which does not exist. It would throw
   * an exception saying "Table with identifier not found: OBJECT_NOT_FOUND"
   */
  @Test
  public void testStreamCreationOnNonExistingTable() {
    try {
      // The table "test" does not exist in this test and we won't create one too.
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());

      boolean masterExceptionThrown = false;
      // Try creating a stream.
      // The following function would look up for a table yugabyte.test by default
      // and try to create stream on that, it would fail eventually.
      try {
        testSubscriber.createStream("proto");
      } catch (MasterErrorException me) {
        // MasterErrorException would be thrown with message:
        // Table with identifier not found: OBJECT_NOT_FOUND
        masterExceptionThrown = true;
      }

      assertTrue(masterExceptionThrown);
    } catch (Exception e) {
      LOG.error("Test to verify failure on creating stream on a non-existing table failed", e);
      fail();
    }
  }

  /**
   * Negative test: Provided a wrong namespace and trying to look for the table there and then
   * create the CDC Stream on that table. This would throw an exception while creating stream
   */
  @Test
  public void testCreateStreamWithInvalidNamespace() {
    try {
      statement.execute("create table test (a int primary key, b int, c numeric);");

      // Dummy insert.
      int res = statement.executeUpdate("insert into test values (1, 2, 20.34);");
      assertEquals(1, res);

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      YBClient myClient = testSubscriber.getSyncClient();
      assertNotNull(myClient);

      // The below table would only be for the table yugabyte.test
      YBTable table = testSubscriber.getTable();
      assertNotNull(table);

      boolean exceptionThrown = false;
      try {
        CreateCDCStreamResponse resp =
          myClient.createCDCStream(table, "non_existing_namespace", "proto", "implicit");
      } catch (Exception e) {
        // The above try block would throw an exception since we are trying to create a stream
        // on a namespace which doesn't exist.
        assertTrue(e.getMessage().contains("YSQL keyspace name not found: non_existing_namespace"));
        exceptionThrown = true;
      }
      assertTrue(exceptionThrown);
    } catch (Exception e) {
      LOG.error("Test to verify failure with invalid namespace failed with exception", e);
      fail();
    }
  }

  /**
   * Test to verify that creating a stream on a table with no primary key fails. While trying to
   * create the CDC stream, an exception would be thrown while creating stream.
   */
  @Test
  public void testStreamCreationWithoutPrimaryKey() {
    try {
      statement.execute("create table test (a int, b int);");

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      YBClient myClient = testSubscriber.getSyncClient();
      assertNotNull(myClient);

      // The below table would only be for the table yugabyte.test
      YBTable table = testSubscriber.getTable();
      assertNotNull(table);

      // After the fix in GitHub Issue [#10945], no exception would be thrown if we try to create
      // a stream on a database if it contains a table with no primary key.
      boolean exceptionThrown = false;
      try {
        CreateCDCStreamResponse resp =
          myClient.createCDCStream(table, DEFAULT_NAMESPACE, "proto", "implicit");
      } catch (Exception e) {
        // The try block would throw an exception since we are trying to create stream on
        // a table with no primary key.
        exceptionThrown = true;
      }
      assertFalse(exceptionThrown);

    } catch (Exception e) {
      LOG.error("Test to verify attaching cdc on a table with no primary key " +
        "failed with exception", e);
      fail();
    }
  }

  @Test
  public void testPollingWithWrongStreamId() {
    try {
      assertFalse(statement.execute("create table test (a int primary key, b int);"));

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream("proto");

      String someRandomStreamId = "75fb51d1va000ib0h88a710v140a4f51";

      // Explicitly changing the automatically generated db stream id.
      testSubscriber.setDbStreamId(someRandomStreamId);

      int dummyInsert = statement.executeUpdate("insert into test values (1, 2);");
      assertEquals(1, dummyInsert);

      List<CdcService.CDCSDKProtoRecordPB> outputList = new ArrayList<>();
      boolean cdcExceptionCaught = false;
      try {
        // CDCErrorException would be thrown since the DB stream Id doesn't exist.
        testSubscriber.getResponseFromCDC(outputList);
      } catch (CDCErrorException ce) {
        assertTrue(ce.getMessage().contains("75fb51d1va000ib0h88a710v140a4f51"));
        // NOT_FOUND[code 1]: Could not find CDC stream
        cdcExceptionCaught = true;
      }
      assertEquals(0, outputList.size());
      assertTrue(cdcExceptionCaught);
    } catch (Exception e) {
      LOG.error("Test to verify failure on polling with wrong DB stream id failed", e);
      fail();
    }
  }

  @Test
  public void testStreamCreationUsingYbAdmin() {
    try {
      String dbStreamId = createDBStreamUsingYbAdmin(getMasterAddresses(), "yugabyte");

      assertNotNull(dbStreamId);
      assertFalse(dbStreamId.isEmpty());
    } catch (Exception e) {
      LOG.error("Test to verify stream creation using yb-admin failed", e);
      fail();
    }
  }

  @Test
  public void testStreamCreationAndDeletionYbAdmin() {
    try {
      String dbStreamId = createDBStreamUsingYbAdmin(getMasterAddresses(), "yugabyte");

      assertNotNull(dbStreamId);
      assertFalse(dbStreamId.isEmpty());

      String deletedStreamId = deleteDBStreamUsingYbAdmin(getMasterAddresses(), dbStreamId);
      assertEquals(dbStreamId, deletedStreamId);
    } catch (Exception e) {
      LOG.error("Test to verify stream creation and deletion via yb-admin failed", e);
      fail();
    }
  }
}
