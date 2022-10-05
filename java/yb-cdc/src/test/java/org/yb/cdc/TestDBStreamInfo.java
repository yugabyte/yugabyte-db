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
import org.yb.client.GetDBStreamInfoResponse;
import org.yb.util.YBTestRunnerNonTsanOnly;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestDBStreamInfo extends CDCBaseClass {
  private final static Logger LOG = LoggerFactory.getLogger(TestDBStreamInfo.class);

  @Before
  public void setUp() throws Exception {
    statement = connection.createStatement();
    statement.execute("drop table if exists test;");
    statement.execute("create table test (a int primary key, b int, c int);");
  }

  @Test
  public void testDBStreamInfoResponse() throws Exception {
    LOG.info("Starting testDBStreamInfoResponse");

    // Inserting a dummy row.
    int rowsAffected = statement.executeUpdate("insert into test values (1, 2, 3);");
    assertEquals(1, rowsAffected);

    CDCSubscriber testSubscriber = new CDCSubscriber(getMasterAddresses());
    testSubscriber.createStream("proto");

    GetDBStreamInfoResponse resp = testSubscriber.getDBStreamInfo();

    assertNotEquals(0, resp.getTableInfoList().size());
    assertNotEquals(0, resp.getNamespaceId().length());
  }
}
