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

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.zip.GZIPOutputStream;

public final class GzipHelpers {

  private GzipHelpers() {
  }

  private static final Logger LOG = LoggerFactory.getLogger(GzipHelpers.class);

  public static void gzipFile(String inputFile, String outputFile) throws IOException {
    if (inputFile.equals(outputFile)) {
      throw new IllegalArgumentException(
          "Input file cannot be the same as output file: " + inputFile);
    }

    byte[] buffer = new byte[1048576];

    try (GZIPOutputStream gzipOut = new GZIPOutputStream(new FileOutputStream(outputFile));
         FileInputStream in = new FileInputStream(inputFile)) {

      int len;
      while ((len = in.read(buffer)) > 0) {
        gzipOut.write(buffer, 0, len);
      }

      gzipOut.finish();
    } catch (IOException ex) {
      File outFile = new File(outputFile);
      if (outFile.exists()) {
        outFile.delete();
      }
      throw ex;
    }
  }

  public static String gzipAndDeleteOriginal(String filePath) throws IOException {
    String gzippedFilePath = filePath + ".gz";
    gzipFile(filePath, gzippedFilePath);
    if (!new File(filePath).delete()) {
      LOG.warn("Failed deleting " + filePath);
    }
    return gzippedFilePath;
  }

  /**
   * @param filePath file path to be gzipped
   * @return the gzipped file path, or the original file path if gzipping failed
   */
  public static String bestEffortGzipFile(String filePath) {
    if (new File(filePath).exists()) {
      try {
        return gzipAndDeleteOriginal(filePath);
      } catch (IOException ex) {
        LOG.info("Failed to gzip file: " + filePath);
        return filePath;
      }
    } else {
      return filePath;
    }
  }

}
