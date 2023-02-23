// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Test;

public class UniverseSpecTest extends FakeDBApplication {

  @Test
  public void testReplaceBeginningPathChanged() {
    String pathToModify = "/opt/yugaware/path/to/opt/yugaware/file";
    String initialRootPath = "/opt/yugaware";
    String finalRootPath = "/root/data";
    String expectedModifiedPath = "/root/data/path/to/opt/yugaware/file";

    String modifiedPath =
        UniverseSpec.replaceBeginningPath(pathToModify, initialRootPath, finalRootPath);
    assertEquals(expectedModifiedPath, modifiedPath);
  }

  @Test
  public void testReplaceBeginningPathUnChanged() {
    String pathToModify = "/opt/yugaware/path/to/opt/path/file";
    String initialRootPath = "/opt/yugaware";
    String finalRootPath = "/opt/yugaware";

    String modifiedPath =
        UniverseSpec.replaceBeginningPath(pathToModify, initialRootPath, finalRootPath);
    assertEquals(pathToModify, modifiedPath);
  }
}
