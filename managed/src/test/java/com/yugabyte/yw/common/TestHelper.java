// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Maps;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Map;
import java.util.UUID;
import play.test.Helpers;

public class TestHelper {
  public static String TMP_PATH = "/tmp/yugaware_tests";

  public static String createTempFile(String basePath, String fileName, String data) {
    FileWriter fw;
    try {
      String filePath = TMP_PATH;
      if (basePath != null) {
        filePath = basePath;
      }
      File tmpFile = new File(filePath, fileName);
      tmpFile.getParentFile().mkdirs();
      fw = new FileWriter(tmpFile);
      fw.write(data);
      fw.close();
      tmpFile.deleteOnExit();
      return tmpFile.getAbsolutePath();
    } catch (IOException ex) {
      return null;
    }
  }

  public static String createTempFile(String fileName, String data) {
    return createTempFile(null, fileName, data);
  }

  public static String createTempFile(String data) {
    String fileName = UUID.randomUUID().toString();
    return createTempFile(fileName, data);
  }

  public static Map<String, Object> testDatabase() {
    return Maps.newHashMap(
        // Needed because we're using 'value' as column name. This makes H2 be happy with that./
        // PostgreSQL works fine with 'value' column as is.
        Helpers.inMemoryDatabase("default", ImmutableMap.of("NON_KEYWORDS", "VALUE")));
  }
}
