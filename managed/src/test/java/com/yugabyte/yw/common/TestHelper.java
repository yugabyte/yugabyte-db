// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Maps;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.slf4j.LoggerFactory;
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

  public static List<ILoggingEvent> captureLogEventsFor(Class<PlatformReplicationManager> clazz) {
    // get Logback Logger
    Logger prmLogger = (Logger) LoggerFactory.getLogger(clazz);

    // create and start a ListAppender
    ListAppender<ILoggingEvent> listAppender = new ListAppender<>();
    listAppender.start();
    prmLogger.addAppender(listAppender);
    return listAppender.list;
  }

  public static Map<String, Object> testDatabase() {
    return Maps.newHashMap(
        // Needed because we're using 'value' as column name. This makes H2 be happy with that./
        // PostgreSQL works fine with 'value' column as is.
        Helpers.inMemoryDatabase("default", ImmutableMap.of("NON_KEYWORDS", "VALUE")));
  }
}
