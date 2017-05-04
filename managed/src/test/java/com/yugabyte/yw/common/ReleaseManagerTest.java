// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;


import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.runners.MockitoJUnitRunner;
import play.Configuration;

import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.yugabyte.yw.common.ConfigHelper.ConfigType.SoftwareReleases;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;
import static org.hamcrest.CoreMatchers.*;

@RunWith(MockitoJUnitRunner.class)
public class ReleaseManagerTest {
  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests/releases";
  static String TMP_DOCKER_STORAGE_PATH = "/tmp/yugaware_tests/docker/releases";

  @InjectMocks
  ReleaseManager releaseManager;

  @Mock
  Configuration appConfig;

  @Mock
  ConfigHelper configHelper;

  @Before
  public void beforeTest() throws IOException {
    new File(TMP_STORAGE_PATH).mkdirs();
    new File(TMP_DOCKER_STORAGE_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
    FileUtils.deleteDirectory(new File(TMP_DOCKER_STORAGE_PATH));
  }

  private void createDummyReleases(List<String> versions, boolean multipleRepos, boolean inDockerPath) {
    versions.forEach((version) -> {
      String versionPath = String.format("%s/%s", TMP_STORAGE_PATH, version);
      new File(versionPath).mkdirs();
      if (inDockerPath) {
        versionPath = TMP_DOCKER_STORAGE_PATH;
      }
      createTempFile(versionPath, "yugabyte.xyz." + version + ".tar.gz", "Sample data");
      if (multipleRepos) {
        createTempFile(versionPath, "devops.xyz." + version + ".tar.gz", "Sample data");
      }
    });
  }

  @Test
  public void testGetLocalReleasesWithValidPath() {
    List<String> versions = ImmutableList.of("0.0.1", "0.0.2", "0.0.3");
    createDummyReleases(versions, false, false);
    Map<String, String> releases = releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(3, releases.size());

    releases.keySet().forEach((version) -> {
      assertTrue(versions.contains(version));
    });
  }

  @Test
  public void testGetLocalReleasesWithMultipleFilesValidPath() {
    List<String> versions = ImmutableList.of("0.0.1", "0.0.2", "0.0.3");
    createDummyReleases(versions, true, false);
    Map<String, String> releases = releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(3, releases.size());

    releases.keySet().forEach((version) -> {
      assertTrue(versions.contains(version));
    });
  }

  @Test
  public void testGetLocalReleasesWithInvalidPath() {
    Map<String, String> releases = releaseManager.getLocalReleases("/foo/bar");
    assertTrue(releases.isEmpty());
  }

  @Test
  public void testLoadReleasesWithoutReleasePath() {
    releaseManager.loadReleasesToDB();
    Mockito.verify(configHelper, times(0)).loadConfigToDB(any(), anyMap());
  }

  @Test
  public void testLoadReleasesWithReleasePath() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false);
    releaseManager.loadReleasesToDB();

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1)).loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap = ImmutableMap.of("0.0.1", TMP_STORAGE_PATH + "/0.0.1/yugabyte.xyz.0.0.1.tar.gz");
    assertEquals(releaseMap.getValue(), expectedMap);
    assertEquals(configType.getValue(), SoftwareReleases);
  }

  @Test
  public void testLoadReleasesWithReleaseAndDockerPath() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(appConfig.getString("yb.docker.release")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false);
    List<String> dockerVersions = ImmutableList.of("0.0.2");
    createDummyReleases(dockerVersions, false, true);
    releaseManager.loadReleasesToDB();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1)).loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap = ImmutableMap.of(
        "0.0.1", TMP_STORAGE_PATH + "/0.0.1/yugabyte.xyz.0.0.1.tar.gz",
        "0.0.2", TMP_STORAGE_PATH + "/0.0.2/yugabyte.xyz.0.0.2.tar.gz");
    assertEquals(releaseMap.getValue(), expectedMap);
    assertEquals(configType.getValue(), SoftwareReleases);

    File dockerStoragePath = new File(TMP_DOCKER_STORAGE_PATH);
    File[] files = dockerStoragePath.listFiles();
    assertEquals(0, files.length);
  }

  @Test
  public void testGetReleaseByVersionWithConfig() {
    when(configHelper.getConfig(SoftwareReleases))
        .thenReturn(ImmutableMap.of("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz"));
    String release = releaseManager.getReleaseByVersion("0.0.1");
    assertThat(release, allOf(notNullValue(),
        equalTo("/path/to/yugabyte-0.0.1.tar.gz")));
  }

  @Test
  public void testGetReleaseByVersionWithoutConfig() {
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(Collections.emptyMap());
    String release = releaseManager.getReleaseByVersion("0.0.1");
    assertNull(release);
  }
}
