package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import java.util.UUID;
import org.junit.Test;

public class ReleaseLocalFilesTest extends FakeDBApplication {
  @Test
  public void testCreateAndGet() {
    UUID uuid = UUID.randomUUID();
    String path = "/test/path";
    ReleaseLocalFile created = ReleaseLocalFile.create(uuid, path);
    ReleaseLocalFile found = ReleaseLocalFile.get(uuid);
    assertEquals(created.getFileUUID(), uuid);
    assertEquals(found.getFileUUID(), uuid);
    assertEquals(created.getLocalFilePath(), path);
    assertEquals(found.getLocalFilePath(), path);
  }
}
