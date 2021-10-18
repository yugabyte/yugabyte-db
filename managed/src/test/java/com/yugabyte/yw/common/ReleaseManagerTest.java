// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.SoftwareReleases;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import java.io.File;
import java.io.IOException;
import java.lang.Object;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.Spy;
import org.mockito.runners.MockitoJUnitRunner;
import play.Configuration;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReleaseManagerTest extends FakeDBApplication {
  static String TMP_STORAGE_PATH = "/tmp/yugaware_tests/releases";
  static String TMP_DOCKER_STORAGE_PATH = "/tmp/yugaware_tests/docker/releases";

  @InjectMocks ReleaseManager releaseManager;

  @Mock Configuration appConfig;

  @Spy ConfigHelper configHelper;

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

  private void createDummyReleases(
      List<String> versions, boolean multipleRepos, boolean inDockerPath) {
    createDummyReleases(versions, multipleRepos, inDockerPath, true, true);
  }

  private void createDummyReleases(
      List<String> versions,
      boolean multipleRepos,
      boolean inDockerPath,
      boolean hasEnterpriseStr,
      boolean withHelmChart) {
    versions.forEach(
        (version) -> {
          String versionPath = String.format("%s/%s", TMP_STORAGE_PATH, version);
          new File(versionPath).mkdirs();
          if (inDockerPath) {
            versionPath = TMP_DOCKER_STORAGE_PATH;
          }
          String eeStr = hasEnterpriseStr ? "ee-" : "";
          createTempFile(
              versionPath, "yugabyte-" + eeStr + version + "-centos-x86_64.tar.gz", "Sample data");
          if (multipleRepos) {
            createTempFile(
                versionPath, "devops.xyz." + version + "-centos-x86_64.tar.gz", "Sample data");
          }
          if (withHelmChart) {
            createTempFile(
                versionPath, "yugabyte-" + version + "-helm.tar.gz", "Sample helm chart data");
          }
        });
  }

  @Test
  public void testRemoveRelease() {
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false, false, false);
    Object metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    Map<String, Object> temp = new HashMap<String, Object>();
    temp.put("0.0.1", metadata);
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(temp);
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(1, releases.size());
    releaseManager.removeRelease("0.0.1");
    releases = releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(0, releases.size());
  }

  @Test
  public void testGetLocalReleasesWithValidPath() {
    List<String> versions = ImmutableList.of("0.0.1", "0.0.2", "0.0.3");
    createDummyReleases(versions, false, false);
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(3, releases.size());
    releases.forEach(
        (version, release) -> {
          assertTrue(versions.contains(version));
          assertNotNull(release.filePath);
          assertNotNull(release.chartPath);
        });
  }

  @Test
  public void testGetLocalReleasesWithMultipleFilesValidPath() {
    List<String> versions = ImmutableList.of("0.0.1", "0.0.2", "0.0.3");
    createDummyReleases(versions, true, false);
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(3, releases.size());
    releases.forEach(
        (version, release) -> {
          assertTrue(versions.contains(version));
          assertNotNull(release.filePath);
          assertNotNull(release.chartPath);
        });
  }

  @Test
  public void testGetLocalReleasesWithInvalidPath() {
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases("/foo/bar");
    assertTrue(releases.isEmpty());
  }

  @Test
  public void testLoadReleasesWithoutReleasePath() {
    releaseManager.importLocalReleases();
    Mockito.verify(configHelper, times(0)).loadConfigToDB(any(), anyMap());
  }

  private void assertReleases(
      Map<String, ReleaseManager.ReleaseMetadata> expected,
      Map<String, ReleaseManager.ReleaseMetadata> actual) {
    assertEquals(expected.size(), actual.size());
    expected.forEach(
        (version, expectedRelease) -> {
          assertTrue(actual.containsKey(version));
          ReleaseManager.ReleaseMetadata actualRelease = actual.get(version);
          assertEquals(version, actualRelease.imageTag);
          assertEquals(expectedRelease.imageTag, actualRelease.imageTag);
          assertEquals(expectedRelease.state, actualRelease.state);
          assertEquals(expectedRelease.filePath, actualRelease.filePath);
          assertEquals(expectedRelease.chartPath, actualRelease.chartPath);
        });
  }

  @Test
  public void testLoadReleasesWithReleasePath() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false);
    releaseManager.importLocalReleases();

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.1",
            ReleaseManager.ReleaseMetadata.create("0.0.1")
                .withFilePath(TMP_STORAGE_PATH + "/0.0.1/yugabyte-ee-0.0.1-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.1/yugabyte-0.0.1-helm.tar.gz"));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithoutChart() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false, true, false);
    releaseManager.importLocalReleases();

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.1",
            ReleaseManager.ReleaseMetadata.create("0.0.1")
                .withFilePath(TMP_STORAGE_PATH + "/0.0.1/yugabyte-ee-0.0.1-centos-x86_64.tar.gz")
                .withChartPath(""));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithReleaseAndDockerPath() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(appConfig.getString("yb.docker.release")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    when(appConfig.getString("yb.helm.packagePath")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false);
    List<String> dockerVersions = ImmutableList.of("0.0.2-b2");
    createDummyReleases(dockerVersions, false, true);
    List<String> dockerVersionsWithoutEe = ImmutableList.of("0.0.3-b3");
    createDummyReleases(dockerVersionsWithoutEe, false, true, false, true);
    releaseManager.importLocalReleases();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.1",
                ReleaseManager.ReleaseMetadata.create("0.0.1")
                    .withFilePath(
                        TMP_STORAGE_PATH + "/0.0.1/yugabyte-ee-0.0.1-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.1/yugabyte-0.0.1-helm.tar.gz"),
            "0.0.2-b2",
                ReleaseManager.ReleaseMetadata.create("0.0.2-b2")
                    .withFilePath(
                        TMP_STORAGE_PATH + "/0.0.2-b2/yugabyte-ee-0.0.2-b2-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.2-b2/yugabyte-0.0.2-b2-helm.tar.gz"),
            "0.0.3-b3",
                ReleaseManager.ReleaseMetadata.create("0.0.3-b3")
                    .withFilePath(
                        TMP_STORAGE_PATH + "/0.0.3-b3/yugabyte-0.0.3-b3-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.3-b3/yugabyte-0.0.3-b3-helm.tar.gz"));

    assertEquals(SoftwareReleases, configType.getValue());
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithReleaseAndDockerPathDuplicate() {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(appConfig.getString("yb.docker.release")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.2-b2");
    createDummyReleases(versions, false, false);
    List<String> dockerVersions = ImmutableList.of("0.0.2-b2");
    createDummyReleases(dockerVersions, false, true);
    releaseManager.importLocalReleases();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.2-b2",
            ReleaseManager.ReleaseMetadata.create("0.0.2-b2")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.2-b2/yugabyte-ee-0.0.2-b2-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.2-b2/yugabyte-0.0.2-b2-helm.tar.gz"));
    assertReleases(expectedMap, releaseMap.getValue());
    assertEquals(SoftwareReleases, configType.getValue());
  }

  @Test
  public void testLoadReleasesWithInvalidDockerPath() {
    try {
      releaseManager.importLocalReleases();
    } catch (RuntimeException re) {
      assertEquals("Unable to look up release files in foo", re.getMessage());
    }
  }

  @Test
  public void testGetReleaseByVersionWithConfig() {
    ReleaseManager.ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(ImmutableMap.of("0.0.1", metadata));
    ReleaseManager.ReleaseMetadata release = releaseManager.getReleaseByVersion("0.0.1");
    assertThat(release.filePath, allOf(notNullValue(), equalTo("/path/to/yugabyte-0.0.1.tar.gz")));
  }

  @Test
  public void testGetReleaseByVersionWithoutConfig() {
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(Collections.emptyMap());
    ReleaseManager.ReleaseMetadata release = releaseManager.getReleaseByVersion("0.0.1");
    assertNull(release);
  }

  @Test
  public void testGetReleaseByVersionWithLegacyConfig() {
    HashMap releases = new HashMap();
    releases.put("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(releases);
    ReleaseManager.ReleaseMetadata release = releaseManager.getReleaseByVersion("0.0.1");
    assertThat(release.filePath, allOf(notNullValue(), equalTo("/path/to/yugabyte-0.0.1.tar.gz")));
  }

  @Test
  public void testAddRelease() {
    ReleaseManager.ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    releaseManager.addReleaseWithMetadata("0.0.1", metadata);
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map releaseInfo = releaseMap.getValue();
    assertTrue(releaseInfo.containsKey("0.0.1"));
    JsonNode releaseMetadata = Json.toJson(releaseInfo.get("0.0.1"));
    assertValue(releaseMetadata, "imageTag", "0.0.1");
  }

  @Test
  public void testAddReleaseWithFullMetadata() {
    ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    metadata.s3 = new ReleaseMetadata.S3Location();
    metadata.s3.paths = new ReleaseMetadata.PackagePaths();
    metadata.s3.paths.x86_64 = "s3://foo";
    metadata.s3.accessKeyId = "abc";
    metadata.s3.secretAccessKey = "abc";
    releaseManager.addReleaseWithMetadata("0.0.1", metadata);
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map releaseInfo = releaseMap.getValue();
    assertTrue(releaseInfo.containsKey("0.0.1"));
    JsonNode releaseMetadata = Json.toJson(releaseInfo.get("0.0.1"));
    assertValue(releaseMetadata, "imageTag", "0.0.1");

    Map<String, Object> allReleases = releaseManager.getReleaseMetadata();
    assertEquals(allReleases.size(), 1);
    Object foundObj = allReleases.get("0.0.1");
    ReleaseMetadata foundMetadata = Json.fromJson(Json.toJson(foundObj), ReleaseMetadata.class);
    assertEquals(foundMetadata.s3.accessKeyId, metadata.s3.accessKeyId);
    assertEquals(foundMetadata.s3.secretAccessKey, metadata.s3.secretAccessKey);
    assertEquals(foundMetadata.s3.paths.x86_64, metadata.s3.paths.x86_64);
  }

  @Test
  public void testAddExistingRelease() {
    ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(ImmutableMap.of("0.0.1", metadata));
    try {
      releaseManager.addReleaseWithMetadata("0.0.1", metadata);
    } catch (RuntimeException re) {
      assertEquals("Release already exists for version [0.0.1]", re.getMessage());
    }
  }
}
