//
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

import com.datastax.driver.core.Row;
import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.PreparedStatement;
import com.google.common.collect.ImmutableList;

import java.util.List;
import java.util.Set;
import java.util.HashSet;
import java.util.UUID;

import org.yb.YBTestRunner;
import org.junit.Test;
import org.junit.Ignore;
import org.junit.runner.RunWith;

import static org.yb.AssertionWrappers.assertTrue;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotEquals;

@RunWith(value=YBTestRunner.class)
public class TestUuidBuiltIn extends BaseCQLTest {

  @Test
  public void testUuidBuiltin() throws Exception {
    session.execute("create table test_uuid (k varchar primary key, v uuid);");
    session.execute("insert into test_uuid (k, v) values ('a', uuid());");
    session.execute("insert into test_uuid (k, v) values ('b', uuid());");
    session.execute("insert into test_uuid (k, v) values ('c', uuid());");

    ResultSet rs = session.execute("select * from test_uuid;");
    assertEquals(3, rs.all().size());
    Set<UUID> uuids = new HashSet<UUID>();
    String queryBase = "select v from test_uuid where k = ";
    List<String> values = ImmutableList.of(" 'a';", " 'b';", " 'c';");
    for (String value : values) {
      UUID uuid = session.execute(queryBase + value).all().get(0).getUUID("v");
      assertFalse(uuids.contains(uuid));
      assertEquals(uuid.version(), 4);
      uuids.add(uuid);
    }
  }

  @Test
  public void testBindForUuidBuiltin() throws Exception {
    session.execute("create table test_uuid_bind (k int primary key, v uuid);");
    PreparedStatement preparedStatement = session.prepare
        ("insert into test_uuid_bind (k, v) values (?, ?);");
    String uuidStr = "11111111-2222-3333-4444-555555555555";
    session.execute(preparedStatement.bind(1, UUID.fromString(uuidStr)));
    ResultSet rs = session.execute("select * from test_uuid_bind;");
    assertEquals(1, rs.all().size());
    UUID uuid =
        session.execute("select v from test_uuid_bind where k = 1;").all().get(0).getUUID("v");
    assertEquals(uuid.toString(), uuidStr);
  }

  @Test
  public void testBindsWithUuidBuiltin() throws Exception {
    session.execute("create table test_uuid_bind (k int primary key, v1 uuid, v2 uuid);");
    PreparedStatement preparedStatement = session.prepare
        ("insert into test_uuid_bind (k, v1, v2) values (?, uuid(), uuid());");
    session.execute(preparedStatement.bind(1));
    session.execute(preparedStatement.bind(2));
    List<Row> rows1 = session.execute("select * from test_uuid_bind where k = 1;").all();
    List<Row> rows2 = session.execute("select * from test_uuid_bind where k = 2;").all();
    assertNotEquals(rows1.get(0).getUUID("v1"), rows1.get(0).getUUID("v2"));
    assertNotEquals(rows2.get(0).getUUID("v1"), rows2.get(0).getUUID("v2"));
    assertNotEquals(rows1.get(0).getUUID("v1"), rows2.get(0).getUUID("v1"));
    assertNotEquals(rows1.get(0).getUUID("v2"), rows2.get(0).getUUID("v2"));
    assertNotEquals(rows1.get(0).getUUID("v1"), rows2.get(0).getUUID("v2"));
    assertNotEquals(rows1.get(0).getUUID("v2"), rows2.get(0).getUUID("v1"));
  }
}
