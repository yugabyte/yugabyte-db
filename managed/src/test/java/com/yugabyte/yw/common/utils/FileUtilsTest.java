// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class FileUtilsTest {

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  @Test
  public void unTarWithinDirSkipsEntriesThatResolveOutsideTheDir() throws Exception {
    Map<String, String> entries = new LinkedHashMap<>();
    entries.put("../escaped.txt", "x");
    entries.put("logs/../../escaped-nested.txt", "x");
    entries.put("./logs/kept.txt", "kept");
    entries.put("/absolute.txt", "abs");
    entries.put("dots..name.txt", "dots");
    File tar = writeTar(entries);
    File out = tmp.newFolder("out");

    List<File> extracted = FileUtils.unTarWithinDir(tar, out);

    assertFalse(new File(tmp.getRoot(), "escaped.txt").exists());
    assertFalse(new File(tmp.getRoot(), "escaped-nested.txt").exists());
    // Extraction carries on past a skipped entry.
    assertEquals("kept", Files.readString(out.toPath().resolve("logs/kept.txt")));
    // An absolute name stays under the dir, as with unTar.
    assertEquals("abs", Files.readString(out.toPath().resolve("absolute.txt")));
    assertEquals("dots", Files.readString(out.toPath().resolve("dots..name.txt")));
    assertEquals(3, extracted.size());
  }

  /**
   * On a well-formed archive, including the "./" root entry of `tar -C dir .`, it matches unTar.
   */
  @Test
  public void unTarWithinDirExtractsAWellFormedArchiveLikeUnTar() throws Exception {
    Map<String, String> entries = new LinkedHashMap<>();
    entries.put("./", null);
    entries.put("./master/", null);
    entries.put("./master/conf/server.conf", "--flag=1");
    entries.put("./tserver/logs/yb-tserver.INFO", "log");
    File tar = writeTar(entries);
    File viaUnTar = tmp.newFolder("unTar");
    File viaWithinDir = tmp.newFolder("withinDir");

    FileUtils.unTar(tar, viaUnTar);
    FileUtils.unTarWithinDir(tar, viaWithinDir);

    assertEquals(relativeFiles(viaUnTar.toPath()), relativeFiles(viaWithinDir.toPath()));
    assertTrue(new File(viaWithinDir, "master/conf/server.conf").isFile());
  }

  /** A null value is a directory entry. Absolute names are kept as written. */
  private File writeTar(Map<String, String> entries) throws IOException {
    File tar = tmp.newFile("test.tar");
    try (OutputStream out = Files.newOutputStream(tar.toPath());
        TarArchiveOutputStream tarOut = new TarArchiveOutputStream(out)) {
      for (Map.Entry<String, String> e : entries.entrySet()) {
        TarArchiveEntry entry = new TarArchiveEntry(e.getKey(), true);
        if (e.getValue() == null) {
          tarOut.putArchiveEntry(entry);
        } else {
          byte[] content = e.getValue().getBytes(StandardCharsets.UTF_8);
          entry.setSize(content.length);
          tarOut.putArchiveEntry(entry);
          tarOut.write(content);
        }
        tarOut.closeArchiveEntry();
      }
      tarOut.finish();
    }
    return tar;
  }

  private static Set<String> relativeFiles(Path dir) throws IOException {
    try (var files = Files.walk(dir)) {
      return files.map(p -> dir.relativize(p).toString()).collect(Collectors.toSet());
    }
  }
}
