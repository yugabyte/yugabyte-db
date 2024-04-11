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

package org.yb.pgsql;

import java.sql.SQLException;
import java.sql.Statement;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

public abstract class TestToastFunctionBase extends TestToastBase {
  private static final Logger LOG = LoggerFactory.getLogger(TestToastFunctionBase.class);
  String object_template = "huge_function_%d";

  @Override
  String selectObject(int index) {
    return String.format(
        "SELECT %s(a) a FROM %s ORDER BY a", String.format(object_template, index), TABLE_NAME);
  }

  @Override
  void createObject(int index, int size, boolean originalTemplate) throws SQLException {
    String name = String.format(object_template, index);

    String functionBody = Stream.generate(() -> " + 0").limit(size).collect(Collectors.joining());
    String functionTemplate;
    if (originalTemplate) {
      functionTemplate =
          String.format("CREATE OR REPLACE FUNCTION %s(x INT) RETURNS INT AS $$", name)
              + "BEGIN "
              + String.format("RETURN x%s; ", functionBody)
              + "END;"
              + "$$ LANGUAGE plpgsql";
    } else {
      functionTemplate =
          String.format("CREATE OR REPLACE FUNCTION %s(x INT) RETURNS INT AS $$", name)
              + "DECLARE"
              + "  i INT = 0;"
              + "BEGIN "
              + String.format("RETURN i%s; ", functionBody)
              + "END;"
              + "$$ LANGUAGE plpgsql";
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute(functionTemplate);
    }
  }
}
