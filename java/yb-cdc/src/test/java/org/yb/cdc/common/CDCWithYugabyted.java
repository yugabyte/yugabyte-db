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

package org.yb.cdc.common;

import com.google.common.net.HostAndPort;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.yb.cdc.util.CDCTestUtils;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.Statement;

/**
 * To test with Yugabyted, all the user need to do is extend CDCWithYugabyted instead of
 * CDCBaseClass
 * <br><br>
 * So instead of the below line <br>
 * <strong>public class XYZ extends CDCBaseClass</strong><br>
 * write <br>
 * <strong>public class XYZ extends CDCWithYugabyted</strong>
 */
public class CDCWithYugabyted {
  protected Connection connection;
  protected Statement statement;

  @BeforeClass
  public static void startYugabyte() {
    CDCTestUtils.startYugabyted();
  }

  @AfterClass
  public static void destroyYugabyte() {
    CDCTestUtils.destroyYugabyted();
  }

  public void setUp() throws Exception {
    connection = DriverManager.getConnection("jdbc:yugabytedb://127.0.0.1:5433/yugabyte?" +
      "user=yugabyte&password=yugabyte");
  }

  protected String getMasterAddresses() {
    return "127.0.0.1:7100";
  }

  protected HostAndPort getTserverHostAndPort() {
    return HostAndPort.fromString("127.0.0.1:9000");
  }
}
