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
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

@RunWith(value=YBTestRunner.class)
public class TestTuple extends BaseCQLTest {

  @Test
  public void allowedAsNameForColumnTableKeyspace() throws Exception {
    session.execute("CREATE KEYSPACE tuple");
    session.execute("CREATE TABLE tuple.tuple (tuple INT PRIMARY KEY)");
    session.execute("INSERT INTO tuple.tuple (tuple) VALUES (1)");
    session.execute("INSERT INTO tuple.tuple (tuple) VALUES (100)");
    session.execute("INSERT INTO tuple.tuple (tuple) VALUES (10)");
    assertQuery("SELECT tuple FROM tuple.tuple;", "Row[1]" + "Row[10]" + "Row[100]");
  }

  // TODO: After #936 replace this with proper tests
  // See also: ql-parser-test.cc
  @Test
  public void notSupported() throws Exception {
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple)");
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple<>)");
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple< >)");
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple<int>)");
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple<int,int>)");
    runInvalidStmt("CREATE TABLE human_resource(id int primary key, name tuple<int,randomstuff>)");
  }
}
