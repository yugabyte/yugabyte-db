/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 */
package org.yb.util;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.util.ArrayList;
import java.util.List;

public class FileUtil {

  private static final Logger LOG = LoggerFactory.getLogger(FileUtil.class);

  public static void bestEffortDeleteIfEmpty(String filePath) {
    File f = new File(filePath);
    if (f.exists() && f.length() == 0) {
      if (!f.delete()) {
        LOG.warn("Could not delete " + filePath + " even though it is empty");
      }
    }
  }

  public static List<String> readLinesFrom(File f) throws IOException {
    if (!f.exists()) {
      return new ArrayList<>();
    }
    try (BufferedReader reader = new BufferedReader(
        new InputStreamReader(new FileInputStream(f)))) {
      List<String> lines = new ArrayList<>();
      String line;
      while ((line = reader.readLine()) != null) {
        lines.add(line);
      }
      return lines;
    }
  }

}
