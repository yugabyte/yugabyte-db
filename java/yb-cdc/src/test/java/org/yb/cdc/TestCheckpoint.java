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

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.common.CDCBaseClass;
import org.yb.cdc.util.CDCSubscriber;
import org.yb.client.GetCheckpointResponse;

import static org.yb.AssertionWrappers.*;
import org.junit.Before;
import org.junit.Test;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestCheckpoint extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestCheckpoint.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  /**
   * Testing the getCheckpoint API.
   * @throws Exception
   */
  @Test
  public void testGetCheckpointResponse() throws Exception {
    LOG.info("Starting testGetCheckpointResponse");

    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    // Dummy insert statement.
    int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
    assertEquals(1, rowsAffected);

    GetCheckpointResponse resp = testSubscriber.getCheckpoint();

    assertNotNull(resp);
  }

  /**
   * Test to verify that we can set a checkpoint when the stream is set in EXPLICIT mode
   */
  @Test
  public void testCheckpointing() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream(
          "proto", "EXPLICIT", "CHANGE"); // setting a stream with PROTO format

      // Dummy insert statement.
      int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
      assertEquals(1, rowsAffected);

      GetCheckpointResponse resp = testSubscriber.getCheckpoint();
      if(resp == null) {
        LOG.error("Null response received as GetCheckpointResponse");
        fail();
      }

      long cpTerm = 2;
      long cpIndex = 9;
      testSubscriber.setCheckpoint(cpTerm, cpIndex, true);

      resp = testSubscriber.getCheckpoint();

      assertEquals(cpTerm, resp.getTerm());
      assertEquals(cpIndex, resp.getIndex());
    } catch (Exception e) {
      LOG.error("Test to verify checkpointing failed with exception", e);
      fail();
    }
  }

  /**
   * Test to verify that if in EXPLICIT mode, when we try to set the checkpoint with a negative
   * index, it won't do anything on the server side and the checkpoint would remain unchanged
   */
  @Test
  public void testSettingNegativeIndexAsCheckpoint() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      testSubscriber.createStream(
          "proto", "EXPLICIT", "CHANGE"); // setting a stream with PROTO format

      // Dummy insert statement.
      int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
      assertEquals(1, rowsAffected);

      int rowsAffected2 = statement.executeUpdate("insert into test values (11, 22, 33);");
      assertEquals(1, rowsAffected2);

      GetCheckpointResponse respBeforeSetting = testSubscriber.getCheckpoint();
      LOG.info("The checkpoint received before setting is " + respBeforeSetting.getTerm() + " "
       + respBeforeSetting.getTerm());
      if (respBeforeSetting == null) {
        LOG.error("Null response received as GetCheckpointResponse");
        fail();
      }

      boolean exceptionReceived = false;
      try {
        testSubscriber.setCheckpoint(1, -3, true);
      }
      catch (Exception e) {
        LOG.info("Received expected exception ", e);
        exceptionReceived = true;
      }
      if (!exceptionReceived) {
        fail();
      }
    }
    catch (Exception e) {
      fail();
    }
  }

  /**
   * Test to verify that trying to set a checkpoint in IMPLICIT mode fails
   */
  @Test
  public void testSettingCheckpointWithImplicit() {
    try {
      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      // Setting the checkpoint type as IMPLICIT.
      testSubscriber.createStream("proto", "IMPLICIT", "CHANGE");

      // Dummy insert.
      int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
      assertEquals(1, rowsAffected);

      GetCheckpointResponse respBeforeSetting = testSubscriber.getCheckpoint();

      long cpTerm = 1;
      long cpIndex = 7;

      testSubscriber.setCheckpoint(cpTerm, cpIndex, true);

      // Checkpoint will be set to the specified value,
      // we are just checking if checkpoint can be set in IMPLICIT mode.
      GetCheckpointResponse respAfterSetting = testSubscriber.getCheckpoint();

      assertNotEquals(respBeforeSetting.getTerm(), respAfterSetting.getTerm());
      assertNotEquals(respBeforeSetting.getIndex(), respAfterSetting.getIndex());

      assertEquals(cpTerm, respAfterSetting.getTerm());
      assertEquals(cpIndex, respAfterSetting.getIndex());
    } catch (Exception e) {
      LOG.error("Test to verify behaviour when setting checkpoint in IMPLICIT mode failed", e);
      fail();
    }
  }
}
