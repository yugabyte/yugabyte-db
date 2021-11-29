/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import org.apache.commons.io.IOUtils;

public class TestUtils {
  public static String readResource(String path) {
    try {
      return IOUtils.toString(
          TestUtils.class.getClassLoader().getResourceAsStream(path), StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Failed to read resource " + path, e);
    }
  }

  public static UUID replaceFirstChar(UUID uuid, char firstChar) {
    char[] chars = uuid.toString().toCharArray();
    chars[0] = firstChar;
    return UUID.fromString(new String(chars));
  }
}
