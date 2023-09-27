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

package org.yb.cdc.util;

import org.apache.commons.configuration.ConfigurationException;
import org.apache.commons.configuration.PropertiesConfiguration;
import org.apache.ibatis.jdbc.ScriptRunner;
import org.awaitility.Awaitility;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.cdc.CDCConsoleSubscriber;
import org.yb.cdc.CmdLineOpts;
import org.yb.cdc.ysql.TestBase;

import java.io.*;
import java.net.URL;
import java.net.URLDecoder;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.sql.Connection;
import java.time.Duration;
import java.util.List;

public class CDCTestUtils {
  private static final Logger LOG = LoggerFactory.getLogger(CDCTestUtils.class);

  private static final Path resourcePath = Paths.get("src", "test", "resources");
  private static final String TEST_RESOURCES_PATH = resourcePath.toFile().getAbsolutePath();

  private static String rootYBDirectory;

  public static CDCConsoleSubscriber initJavaClient(List outputList) throws Exception {
    return runJavaClient(outputList, TEST_RESOURCES_PATH + "/config.properties");
  }

  private static CDCConsoleSubscriber runJavaClient(List outputList, String configFilePath)
  throws Exception {
    String[] args = {"-table_name", "yugabyte.test", "-config_file", configFilePath};

    CmdLineOpts configuration = CmdLineOpts.createFromArgs(args);
    CDCSubscriberClient testClient = new CDCSubscriberClient(outputList);
    CDCConsoleSubscriber cdcObject = new CDCConsoleSubscriber(configuration, testClient);

    LOG.info("Starting CDCConsoleSubscriber...");

    new Thread(cdcObject::run).start();

    return cdcObject;
  }

  /**
   * This function clears the streamID in the passed dummy_file.properties file via command line.
   * If no path is passed (as in case of tests), it would automatically go over and read the
   * dummy_file.properties file in the resources directory of tests.
   *
   * @param customConfigPath The path to the properties file consisting of the required
   *                         configuration settings.
   *
   */
  public static void cleanStreamIdOnly(String customConfigPath) {
    try {
      PropertiesConfiguration propConfig;

      if (customConfigPath == null || customConfigPath.isEmpty()) {
        LOG.error("Provide a valid properties file path");
        System.exit(0);
      }

      propConfig = new PropertiesConfiguration(customConfigPath);
      propConfig.setProperty("stream.id", "");
      propConfig.save();
    } catch (ConfigurationException ce) {
      ce.printStackTrace();
    }
  }


  public static void clearStreamId(String customConfigPath) {
    cleanStreamIdOnly(customConfigPath);
  }

  /**
   * This function runs a SQL script using ScriptRunner on the specified connection.
   *
   * @param conn A Connection to connect to and execute the SQL script
   * @param fileName File name of the SQL script with complete path in reference to the resources
   *                 directory in the test directory of this project
   * @see Connection
   * @see ScriptRunner
   */
  public static void runSqlScript(Connection conn, String fileName) {
    LOG.info("Running the SQL script: " + fileName);
    ScriptRunner sr = new ScriptRunner(conn);
    sr.setAutoCommit(true);

    // This prevents ScriptWriter from printing the queries on terminal or logging them either.
    sr.setLogWriter(null);

    final String sqlFile = TEST_RESOURCES_PATH + "/"+ fileName;
    try {
      Reader reader = new BufferedReader(new FileReader(sqlFile));
      sr.runScript(reader);
    } catch (FileNotFoundException f) {
      f.printStackTrace();
    }
  }

  private static String urlToPath(URL u) {
    try {
      return URLDecoder.decode(u.getPath(), "UTF-8");
    } catch (UnsupportedEncodingException e) {
      throw new RuntimeException(e);
    }
  }

  private static String findYbSrcRootContaining(String initialPath) {
    File currentPath = new File(initialPath);
    while (currentPath != null) {
      if (new File(currentPath, "yb_build.sh").exists() &&
        new File(currentPath, "build-support").exists()) {
        return currentPath.getAbsolutePath();
      }
      currentPath = currentPath.getParentFile();
    }
    return null;
  }

  /**
   * This function, when called, iterates over the parent directories and finds out the root
   * directory "yugabyte-db" and returns the path to it. The path to the root directory is useful
   * in executing yugabyted, yb-ctl, etc.
   *
   * @returns The absolute path to the root directory yugtabyte-db
   * @throws RuntimeException When it is unable to find the root directory
   */
  public static synchronized String findRootYBDirectory() throws Exception {
    if (rootYBDirectory != null) {
      return rootYBDirectory;
    }

    final URL myUrl = TestBase.class.getProtectionDomain().getCodeSource().getLocation();
    final String pathToCode = urlToPath(myUrl);
    final String currentDir = System.getProperty("user.dir");

    // Try to find the YB directory root by navigating upward from either the source code location,
    // or, if that does not work, from the current directory.
    for (String initialPath : new String[] { pathToCode, currentDir }) {
      // Cache the root dir so that we don't have to find it every time.
      rootYBDirectory = findYbSrcRootContaining(initialPath);
      if (rootYBDirectory != null) {
        return rootYBDirectory;
      }
    }
    throw new RuntimeException(
      "Unable to find build dir! myUrl=" + myUrl + ", currentDir=" + currentDir);
  }

  /**
   * This function starts a single node cluster using yugabyted. The flow is that it initially
   * calls destroyYugabyted() to destroy any existing cluster which could be running and then
   * creates a new cluster itself.
   */
  public static void startYugabyted() {
    try {
      LOG.info("Destroying yugabyted if running");
      destroyYugabyted();
      String ybRootDir = (rootYBDirectory == null) ? findRootYBDirectory() : rootYBDirectory;
      Process process = Runtime.getRuntime().exec(ybRootDir + "/bin/yugabyted start");

      int exitCode = process.waitFor();
      if (exitCode == 0) {
        LOG.info("yugabyted started successfully");
      } else {
        throw new Exception("yugabyted couldn't start, check if yb_build.sh is successful");
      }

    } catch (Exception e) {
      LOG.error("Exception caught while starting yugabyted", e);
      System.exit(0);
    }
  }

  /**
   * This function destroys any existing yugabyted cluster (a cluster with a single node).
   */
  public static void destroyYugabyted() {
    try {
      String ybRootDir = (rootYBDirectory == null) ? findRootYBDirectory() : rootYBDirectory;
      Process process = Runtime.getRuntime().exec(ybRootDir + "/bin/yugabyted destroy");

      int exitCode = process.waitFor();
      if (exitCode == 0) {
        LOG.info("yugabyted destroyed successfully");
      } else {
        if (exitCode == 1) {
          LOG.error("yugabyted cluster not running");
        } else {
          throw new Exception("yugabyted cluster couldn't be destroyed");
        }
      }
    } catch (Exception e) {
      LOG.error("Exception caught while stopping yugabyted", e);
      System.exit(0);
    }
  }

  /**
   * Helper function to wait for a specified duration
   * @param seconds the amount of seconds to wait
   */
  public static void waitFor(long seconds) {
    Awaitility.await()
      .pollDelay(Duration.ofSeconds(seconds))
      .atMost(Duration.ofSeconds(seconds + 1))
      .until(() -> { return true; });
  }
}
