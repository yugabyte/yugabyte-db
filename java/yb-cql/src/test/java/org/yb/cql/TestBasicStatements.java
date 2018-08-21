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

import org.junit.Ignore;
import org.junit.Test;

import com.datastax.driver.core.Session;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestBasicStatements extends BaseCQLTest {
  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Create table ...");
    session.execute("CREATE TABLE human_resource1(id int primary key, name varchar);");
  }

  // We need to work on reporting error from SQL before activating this test.
  @Test
  public void testInvalidStatement() throws Exception {
    LOG.info("Execute nothing ...");
    thrown.expect(com.datastax.driver.core.exceptions.SyntaxError.class);
    thrown.expectMessage("Invalid SQL Statement");
    session.execute("NOTHING");
  }

  @Test
  public void testUnsupportedProtocol() throws Exception {
    thrown.expect(com.datastax.driver.core.exceptions.UnsupportedProtocolVersionException.class);
    Session s = getDefaultClusterBuilder()
                .allowBetaProtocolVersion()
                .build()
                .connect();
  }
}
