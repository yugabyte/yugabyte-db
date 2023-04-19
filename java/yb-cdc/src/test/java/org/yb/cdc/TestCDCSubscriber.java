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
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class TestCDCSubscriber extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestCDCSubscriber.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  @Test
  public void testSettingInvalidFormat() throws Exception {
    LOG.info("Starting testSettingInvalidStreamFormat");

    boolean runtimeExceptionThrown = false;
    try {
      // Dummy insert statement.
      int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
      assertEquals(1, rowsAffected);

      CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
      // This will throw a RuntimeException currently, but on the server side, if anything other
      // than proto is specified then it would be defaulted to json.
      testSubscriber.createStream("some.invalid.format");
    } catch (RuntimeException re) {
      runtimeExceptionThrown = true;
    }

    assertTrue(runtimeExceptionThrown);
  }
}
