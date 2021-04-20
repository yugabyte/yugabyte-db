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
package org.yb.multiapi;

import java.util.Map;
import com.datastax.driver.core.Session;
import org.junit.BeforeClass;
import org.junit.Before;
import org.junit.After;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.pgsql.BasePgSQLTest;
import org.yb.cql.BaseCQLTest;

public class TestMultiAPIBase extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestMultiAPIBase.class);

  public class CQLApi extends BaseCQLTest {
    Session getSession() { return super.session; }
  };

  protected CQLApi cqlApi;
  protected Session cqlSession;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("start_cql_proxy", "true");
    return flagMap;
  }

  @BeforeClass
  public static void setUpBeforeClass() throws Exception {
    BaseCQLTest.setUpBeforeClass();
  }

  @Before
  public void setUpCql() throws Exception {
    cqlApi = new CQLApi();
    cqlApi.setUpCqlClient();
    cqlSession = cqlApi.getSession();
  }

  @After
  public void tearDownCql() throws Exception {
    cqlApi.tearDownAfter();
  }
}
