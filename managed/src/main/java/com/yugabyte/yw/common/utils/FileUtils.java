package com.yugabyte.yw.common.utils;

import static java.nio.file.StandardCopyOption.REPLACE_EXISTING;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.PlatformServiceException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import org.apache.commons.io.IOUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import play.libs.Json;

public class FileUtils {

  public static final Logger LOG = LoggerFactory.getLogger(FileUtils.class);

  public static String readResource(String filePath, Environment environment) {
    try (InputStream inputStream = environment.resourceAsStream(filePath)) {
      return IOUtils.toString(inputStream, StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Unable to read file " + filePath, e);
    }
  }

  public static void writeStringToFile(File file, String contents) throws Exception {
    try (FileWriter writer = new FileWriter(file)) {
      writer.write(contents);
    }
  }

  /**
   * Extracts the name and extension parts of a file name.
   *
   * <p>The resulting string is the rightmost characters of fullName, starting with the first
   * character after the path separator that separates the path information from the name and
   * extension.
   *
   * <p>The resulting string is equal to fullName, if fullName contains no path.
   *
   * @param fullName
   * @return
   */
  public static String getFileName(String fullName) {
    if (fullName == null) {
      return null;
    }
    int delimiterIndex = fullName.lastIndexOf(File.separatorChar);
    return delimiterIndex >= 0 ? fullName.substring(delimiterIndex + 1) : fullName;
  }

  public static String getFileChecksum(String file) throws IOException, NoSuchAlgorithmException {
    FileInputStream fis = new FileInputStream(file);
    byte[] byteArray = new byte[1024];
    int bytesCount = 0;

    MessageDigest digest = MessageDigest.getInstance("MD5");

    while ((bytesCount = fis.read(byteArray)) != -1) {
      digest.update(byteArray, 0, bytesCount);
    }

    fis.close();

    byte[] bytes = digest.digest();
    StringBuilder sb = new StringBuilder();
    for (byte b : bytes) {
      sb.append(Integer.toString((b & 0xff) + 0x100, 16).substring(1));
    }
    return sb.toString();
  }

  public static List<File> listFiles(Path backupDir, String pattern) throws IOException {
    try (DirectoryStream<Path> directoryStream = Files.newDirectoryStream(backupDir, pattern)) {
      return StreamSupport.stream(directoryStream.spliterator(), false)
          .map(Path::toFile)
          .sorted(File::compareTo)
          .collect(Collectors.toList());
    }
  }

  public static void moveFile(Path source, Path destination) throws IOException {
    Files.move(source, destination, REPLACE_EXISTING);
  }

  public static void writeJsonFile(String filePath, ArrayNode json) {
    writeFile(filePath, Json.prettyPrint(json));
  }

  public static void writeFile(String filePath, String contents) {
    try (FileWriter file = new FileWriter(filePath)) {
      file.write(contents);
      file.flush();
      LOG.info("Written: {}", filePath);
    } catch (IOException e) {
      LOG.error("Unable to write: {}", filePath);
      throw new RuntimeException(e.getMessage());
    }
  }

  /** deleteDirectory deletes entire directory recursively. */
  public static boolean deleteDirectory(File directoryToBeDeleted) {
    File[] allContents = directoryToBeDeleted.listFiles();
    if (allContents != null) {
      for (File file : allContents) {
        deleteDirectory(file);
      }
    }
    return directoryToBeDeleted.delete();
  }

  public static InputStream getInputStreamOrFail(File file) {
    try {
      return new FileInputStream(file);
    } catch (FileNotFoundException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
