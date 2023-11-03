/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.Map;
import java.util.Set;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;

public class DevopsReleaseManifestTest {

  private static final Map<String, Set<String>> EXPECTED_MISSING_FILES =
      ImmutableMap.<String, Set<String>>builder()
          .put(
              ".",
              ImmutableSet.of(
                  "ansible_requirements.yml",
                  "create_instance.yml",
                  "local_role.yml",
                  "python3_requirements.txt",
                  "replicated.yml",
                  "version.txt",
                  "yb_release",
                  "yb_release.py",
                  "yb_release_manifest.json"))
          .put(
              "bin",
              ImmutableSet.of(
                  "ansible_runner.sh",
                  "diagnostics.sh",
                  "filter_ansible_output.py",
                  "freeze_python_requirements.sh",
                  "refresh_roles_in_release_manifest.py",
                  "remove_leaf_yaml_nodes.py",
                  "run_in_virtualenv.sh",
                  "run_tests.sh",
                  "yb_platform_util.py"))
          .build();

  private static final Set<String> PREFIXES_TO_SKIP =
      ImmutableSet.of("build", "python_virtual_env", "tests", "third-party", "venv", ".idea");

  private static final Set<String> FILE_EXTENSIONS_TO_SKIP =
      ImmutableSet.of("gitignore", "retry", "pyc", "orig");

  @Test
  public void testReleaseManifestFiles() throws IOException {
    File devopsDir = new File("devops");
    assertTrue(
        devopsDir.getAbsolutePath() + " is not a directory - check your working directory.",
        devopsDir.isDirectory());

    File releaseManifest = new File(devopsDir, "yb_release_manifest.json");
    assertTrue(releaseManifest.getAbsolutePath() + " not found.", releaseManifest.isFile());

    ObjectMapper objectMapper = new ObjectMapper();
    JsonNode releaseManifestJson = objectMapper.readTree(releaseManifest);
    Map<String, Set<String>> releaseManifestFiles =
        objectMapper.convertValue(
            releaseManifestJson, new TypeReference<Map<String, Set<String>>>() {});
    checkDirectory("", devopsDir, releaseManifestFiles);
  }

  private void checkDirectory(
      String prefix, File dir, Map<String, Set<String>> releaseManifestFiles) {
    String queryPrefix = StringUtils.isEmpty(prefix) ? "." : prefix;
    Set<String> files = releaseManifestFiles.getOrDefault(queryPrefix, Collections.emptySet());
    Set<String> expectedMissingFiles =
        EXPECTED_MISSING_FILES.getOrDefault(queryPrefix, Collections.emptySet());
    if (files.contains(prefix + "/*")) {
      // We include everything from this dir
      return;
    }
    checkFilesInDir(prefix, dir, files, expectedMissingFiles);
    File[] childDirs = dir.listFiles(File::isDirectory);
    if (childDirs == null) {
      return;
    }
    for (File childDir : childDirs) {
      String childPrefix =
          StringUtils.isEmpty(prefix) ? childDir.getName() : prefix + "/" + childDir.getName();
      if (PREFIXES_TO_SKIP.contains(childPrefix)) {
        return;
      }
      checkDirectory(childPrefix, childDir, releaseManifestFiles);
    }
  }

  private void checkFilesInDir(
      String prefix, File dir, Set<String> files, Set<String> expectedMissingFiles) {
    File[] childFiles = dir.listFiles(File::isFile);
    if (childFiles == null) {
      return;
    }
    for (File childFile : childFiles) {
      String extension = FilenameUtils.getExtension(childFile.getName());
      if (StringUtils.isNotEmpty(extension) && FILE_EXTENSIONS_TO_SKIP.contains(extension)) {
        continue;
      }
      String fileName = childFile.getName();
      String releaseManifestFileName =
          StringUtils.isEmpty(prefix) ? fileName : prefix + "/" + fileName;
      if (!files.contains(releaseManifestFileName) && !expectedMissingFiles.contains(fileName)) {
        fail("File " + fileName + " from " + prefix + " is missing from devops release manifest");
      }
      if (files.contains(releaseManifestFileName) && expectedMissingFiles.contains(fileName)) {
        fail(
            "File "
                + fileName
                + " from "
                + prefix
                + " is present i devops release manifest, but is expected to be missing");
      }
    }
  }
}
