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
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.fail;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestUseKeyspace extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestUseKeyspace.class);

  private void testCreateExistingKeyspace(String keyspace) throws Exception {
    try {
      createKeyspace(keyspace);
      fail("CREATE KEYSPACE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected InvalidQuery exception", e);
    }
  }

  private void testUseKeyspace(String keyspace, boolean create) throws Exception {
    if (create) {
      createKeyspace(keyspace);
    }
    useKeyspace(keyspace);
    assertEquals(keyspace, session.getLoggedKeyspace());
  }

  private void testUseInvalidKeyspace(String keyspace) throws Exception {
    try {
      useKeyspace(keyspace);
      fail("USE \"" + keyspace + "\" did not fail");
    } catch (com.datastax.driver.core.exceptions.InvalidQueryException e) {
      LOG.info("Expected InvalidQuery exception", e);
    }
  }

  @Test
  public void testUseKeyspace() throws Exception {
    LOG.info("Begin test testUseKeyspace()");

    // Use existing system keyspace.
    testUseKeyspace("system", false);

    // Use existing system_schema keyspace.
    testUseKeyspace("system_schema", false);

    // Create new keyspace and use it.
    testUseKeyspace("test_keyspace", true);

    // Use a non-existent keyspace.
    testUseInvalidKeyspace("no_such_keyspace");

    LOG.info("End test testUseKeyspace()");
  }

  @Test
  public void testKeyspaceDoubleCreation() throws Exception {
    LOG.info("Begin test testKeyspaceDoubleCreation()");

    // Create new keyspace and use it.
    testUseKeyspace("new_keyspace", true);
    // Try to create already existing keyspace.
    testCreateExistingKeyspace("new_keyspace");

    // Try to create already existing default test keyspace.
    testCreateExistingKeyspace(DEFAULT_TEST_KEYSPACE);

    LOG.info("End test testKeyspaceDoubleCreation()");
  }
}
