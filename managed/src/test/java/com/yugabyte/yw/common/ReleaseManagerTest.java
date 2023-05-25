// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.SoftwareReleases;
import static com.yugabyte.yw.common.ConfigHelper.ConfigType.YbcSoftwareReleases;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
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
import org.mockito.invocation.InvocationOnMock;
import org.mockito.junit.MockitoJUnitRunner;
import org.mockito.stubbing.Answer;
import play.Environment;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReleaseManagerTest extends FakeDBApplication {
  String TMP_STORAGE_PATH = "/tmp/yugaware_tests/" + getClass().getSimpleName() + "/releases";
  String TMP_DOCKER_STORAGE_PATH =
      "/tmp/yugaware_tests/" + getClass().getSimpleName() + "/docker/releases";
  String TMP_REPLACE_STORAGE_PATH =
      "/tmp/yugaware_replace_tests/" + getClass().getSimpleName() + "/releases";

  @InjectMocks ReleaseManager releaseManager;

  @Mock Config appConfig;

  @Spy ConfigHelper configHelper;

  @Mock RuntimeConfGetter confGetter;

  @Mock Environment environment;

  @Before
  public void beforeTest() throws IOException {
    new File(TMP_STORAGE_PATH).mkdirs();
    new File(TMP_REPLACE_STORAGE_PATH).mkdirs();
    new File(TMP_DOCKER_STORAGE_PATH).mkdirs();
    String versionJson = "{\"version_number\": \"2.16.3.4\", \"build_number\":\"12\"}";
    when(environment.resourceAsStream("version_metadata.json"))
        .thenAnswer(
            new Answer<InputStream>() {
              @Override
              public InputStream answer(InvocationOnMock invocation) {
                return new ByteArrayInputStream(versionJson.getBytes(StandardCharsets.UTF_8));
              }
            });
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
    FileUtils.deleteDirectory(new File(TMP_DOCKER_STORAGE_PATH));
  }

  private void createDummyReleases(
      List<String> versions, boolean multipleRepos, boolean inDockerPath) {
    createDummyReleases(versions, multipleRepos, inDockerPath, true, true, false);
  }

  private void createDummyReleases(
      List<String> versions,
      boolean multipleRepos,
      boolean inDockerPath,
      boolean hasEnterpriseStr,
      boolean withHelmChart,
      boolean withAarch) {
    createDummyReleases(
        versions,
        multipleRepos,
        inDockerPath,
        hasEnterpriseStr,
        withHelmChart,
        withAarch,
        "centos");
  }

  private void createDummyReleases(
      List<String> versions,
      boolean multipleRepos,
      boolean inDockerPath,
      boolean hasEnterpriseStr,
      boolean withHelmChart,
      boolean withAarch,
      String os) {
    versions.forEach(
        (version) -> {
          String versionPath = String.format("%s/%s", TMP_STORAGE_PATH, version);
          new File(versionPath).mkdirs();
          if (inDockerPath) {
            versionPath = TMP_DOCKER_STORAGE_PATH;
          }
          String eeStr = hasEnterpriseStr ? "ee-" : "";
          createTempFile(
              versionPath,
              "yugabyte-" + eeStr + version + "-" + os + "-x86_64.tar.gz",
              "Sample data");
          if (multipleRepos) {
            createTempFile(
                versionPath, "devops.xyz." + version + "-" + os + "-x86_64.tar.gz", "Sample data");
          }
          if (withHelmChart) {
            createTempFile(
                versionPath, "yugabyte-" + version + "-helm.tar.gz", "Sample helm chart data");
          }
          if (withAarch) {
            createTempFile(
                versionPath,
                "yugabyte-" + eeStr + version + "-" + os + "-aarch64.tar.gz",
                "Sample data");
          }
        });
  }

  @Test
  public void testRemoveReleaseLocal() {
    List<String> versions = ImmutableList.of("0.0.1");
    createDummyReleases(versions, false, false, false, false, false);
    Object metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy(
            "0.0.1", TMP_STORAGE_PATH + "/path/to/yugabyte-0.0.1.tar.gz");
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
  public void testRemoveReleaseRemote() {
    List<String> versions = ImmutableList.of("0.0.9");
    Object metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.9", "s3://path/to/yugabyte-0.0.9.tar.gz");
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    Map<String, Object> temp = new HashMap<String, Object>();
    temp.put("0.0.9", metadata);
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(temp);
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(0, releases.size());
    Map<String, Object> allReleases = releaseManager.getReleaseMetadata();
    assertEquals(1, allReleases.size());
    releaseManager.removeRelease("0.0.9");
    allReleases = releaseManager.getReleaseMetadata();
    assertEquals(0, allReleases.size());
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
  public void testGetLocalReleasesWithMultipleArchitectureValidPath() {
    List<String> versions = ImmutableList.of("0.0.1", "0.0.2", "0.0.3");
    createDummyReleases(versions, true, false, true, true, true);
    Map<String, ReleaseManager.ReleaseMetadata> releases =
        releaseManager.getLocalReleases(TMP_STORAGE_PATH);
    assertEquals(3, releases.size());
    releases.forEach(
        (version, release) -> {
          assertTrue(versions.contains(version));
          assertNotNull(release.filePath);
          assertNotNull(release.chartPath);
          assertEquals(2, release.packages.size());
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
          assertEquals(expectedRelease.packages.size(), actualRelease.packages.size());
          for (int i = 0; i < expectedRelease.packages.size(); i++) {
            assertEquals(expectedRelease.packages.get(i).path, actualRelease.packages.get(i).path);
            assertEquals(expectedRelease.packages.get(i).arch, actualRelease.packages.get(i).arch);
          }
        });
  }

  @Test
  public void testLoadReleasesWithReleasePath() throws IOException {
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    List<String> versions = ImmutableList.of("0.0.0.1-b1");
    createDummyReleases(versions, false, false);
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.1-b1",
            ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-0.0.0.1-b1-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz",
                    Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithLinuxOSReleasePath() throws IOException {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.1-b1");
    createDummyReleases(versions, false, false, true, true, false, "linux");
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.1-b1",
            ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-linux-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-0.0.0.1-b1-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-linux-x86_64.tar.gz",
                    Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithEl8OSReleasePath() throws IOException {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.1-b1");
    createDummyReleases(versions, false, false, true, true, false, "el8");
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.1-b1",
            ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-el8-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-0.0.0.1-b1-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-el8-x86_64.tar.gz",
                    Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithoutChart() throws IOException {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.1-b1");
    createDummyReleases(versions, false, false, true, false, false);
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());

    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.1-b1",
            ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz")
                .withChartPath("")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz",
                    Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithReleaseAndDockerPath() {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(appConfig.getString("yb.docker.release")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    when(appConfig.getString("yb.helm.packagePath")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.1-b1");
    createDummyReleases(versions, false, false);
    List<String> dockerVersions = ImmutableList.of("0.0.0.2-b2");
    createDummyReleases(dockerVersions, false, true);
    List<String> dockerVersionsWithoutEe = ImmutableList.of("0.0.0.3-b3");
    createDummyReleases(dockerVersionsWithoutEe, false, true, false, true, false);
    List<String> multipleVersionRelease = ImmutableList.of("0.0.0.4-b4");
    createDummyReleases(multipleVersionRelease, false, false, false, true, true);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.1-b1",
                ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1")
                    .withFilePath(
                        TMP_STORAGE_PATH
                            + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.0.1-b1/yugabyte-0.0.0.1-b1-helm.tar.gz")
                    .withPackage(
                        TMP_STORAGE_PATH
                            + "/0.0.0.1-b1/yugabyte-ee-0.0.0.1-b1-centos-x86_64.tar.gz",
                        Architecture.x86_64),
            "0.0.0.2-b2",
                ReleaseManager.ReleaseMetadata.create("0.0.0.2-b2")
                    .withFilePath(
                        TMP_STORAGE_PATH
                            + "/0.0.0.2-b2/yugabyte-ee-0.0.0.2-b2-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-helm.tar.gz")
                    .withPackage(
                        TMP_STORAGE_PATH
                            + "/0.0.0.2-b2/yugabyte-ee-0.0.0.2-b2-centos-x86_64.tar.gz",
                        Architecture.x86_64),
            "0.0.0.3-b3",
                ReleaseManager.ReleaseMetadata.create("0.0.0.3-b3")
                    .withFilePath(
                        TMP_STORAGE_PATH + "/0.0.0.3-b3/yugabyte-0.0.0.3-b3-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.0.3-b3/yugabyte-0.0.0.3-b3-helm.tar.gz")
                    .withPackage(
                        TMP_STORAGE_PATH + "/0.0.0.3-b3/yugabyte-0.0.0.3-b3-centos-x86_64.tar.gz",
                        Architecture.x86_64),
            "0.0.0.4-b4",
                ReleaseManager.ReleaseMetadata.create("0.0.0.4-b4")
                    .withFilePath(
                        TMP_STORAGE_PATH + "/0.0.0.4-b4/yugabyte-0.0.0.4-b4-centos-x86_64.tar.gz")
                    .withChartPath(TMP_STORAGE_PATH + "/0.0.0.4-b4/yugabyte-0.0.0.4-b4-helm.tar.gz")
                    .withPackage(
                        TMP_STORAGE_PATH + "/0.0.0.4-b4/yugabyte-0.0.0.4-b4-centos-x86_64.tar.gz",
                        Architecture.x86_64)
                    .withPackage(
                        TMP_STORAGE_PATH + "/0.0.0.4-b4/yugabyte-0.0.0.4-b4-centos-aarch64.tar.gz",
                        Architecture.aarch64));

    assertEquals(SoftwareReleases, configType.getValue());
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testLoadReleasesWithReleaseAndDockerPathDuplicate() throws IOException {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    when(appConfig.getString("yb.docker.release")).thenReturn(TMP_DOCKER_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.2-b2");
    createDummyReleases(versions, false, false);
    List<String> dockerVersions = ImmutableList.of("0.0.0.2-b2");
    createDummyReleases(dockerVersions, false, true);
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.2-b2",
            ReleaseManager.ReleaseMetadata.create("0.0.0.2-b2")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-ee-0.0.0.2-b2-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-ee-0.0.0.2-b2-centos-x86_64.tar.gz",
                    Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
    assertEquals(SoftwareReleases, configType.getValue());
  }

  @Test
  public void testLoadReleasesWithAlmaPath() throws IOException {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    when(appConfig.getString("yb.releases.path")).thenReturn(TMP_STORAGE_PATH);
    List<String> versions = ImmutableList.of("0.0.0.2-b2");
    createDummyReleases(versions, false, false, false, true, true, "almalinux8");
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    releaseManager.importLocalReleases();
    verify(mockGFlagsValidation, times(1))
        .fetchGFlagFilesFromTarGZipInputStream(any(), any(), any(), any());
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap =
        ImmutableMap.of(
            "0.0.0.2-b2",
            ReleaseManager.ReleaseMetadata.create("0.0.0.2-b2")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-almalinux8-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-almalinux8-x86_64.tar.gz",
                    Architecture.x86_64)
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.2-b2/yugabyte-0.0.0.2-b2-almalinux8-aarch64.tar.gz",
                    Architecture.aarch64));
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
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    ReleaseManager.ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy(
            "0.0.0.1-b1", "/path/to/yyugabyte-0.0.0.1-b1.tar.gz");
    releaseManager.addReleaseWithMetadata("0.0.0.1-b1", metadata);
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map releaseInfo = releaseMap.getValue();
    assertTrue(releaseInfo.containsKey("0.0.0.1-b1"));
    JsonNode releaseMetadata = Json.toJson(releaseInfo.get("0.0.0.1-b1"));
    assertValue(releaseMetadata, "imageTag", "0.0.0.1-b1");
  }

  @Test
  public void testAddReleaseVersionMoreThanYbaVersionDisallowed() {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    ReleaseMetadata metadata = ReleaseMetadata.create("99.99.99.99-b99");
    assertPlatformException(
        () -> releaseManager.addReleaseWithMetadata("99.99.99.99-b99", metadata));
    Mockito.verify(configHelper, times(0)).loadConfigToDB(any(), any());
  }

  @Test
  public void testAddReleaseVersionMoreThanYbaVersionAllowed() {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(true);
    ReleaseMetadata metadata = ReleaseMetadata.create("99.99.99.99-b99");
    releaseManager.addReleaseWithMetadata("99.99.99.99-b99", metadata);
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map releaseInfo = releaseMap.getValue();
    assertTrue(releaseInfo.containsKey("99.99.99.99-b99"));
    JsonNode releaseMetadata = Json.toJson(releaseInfo.get("99.99.99.99-b99"));
    assertValue(releaseMetadata, "imageTag", "99.99.99.99-b99");
  }

  @Test
  public void testAddReleaseWithFullMetadata() {
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.0.1-b1");
    metadata.s3 = new ReleaseMetadata.S3Location();
    metadata.s3.paths = new ReleaseMetadata.PackagePaths();
    metadata.s3.paths.x86_64 = "s3://foo";
    metadata.s3.accessKeyId = "abc";
    metadata.s3.secretAccessKey = "abc";
    releaseManager.addReleaseWithMetadata("0.0.0.1-b1", metadata);
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map releaseInfo = releaseMap.getValue();
    assertTrue(releaseInfo.containsKey("0.0.0.1-b1"));
    JsonNode releaseMetadata = Json.toJson(releaseInfo.get("0.0.0.1-b1"));
    assertValue(releaseMetadata, "imageTag", "0.0.0.1-b1");

    Map<String, Object> allReleases = releaseManager.getReleaseMetadata();
    assertEquals(allReleases.size(), 1);
    Object foundObj = allReleases.get("0.0.0.1-b1");
    ReleaseMetadata foundMetadata = Json.fromJson(Json.toJson(foundObj), ReleaseMetadata.class);
    assertEquals(foundMetadata.s3.accessKeyId, metadata.s3.accessKeyId);
    assertEquals(foundMetadata.s3.secretAccessKey, metadata.s3.secretAccessKey);
    assertEquals(foundMetadata.s3.paths.x86_64, metadata.s3.paths.x86_64);
  }

  @Test
  public void testAddExistingRelease() {
    ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy(
            "0.0.0.1-b1", "/path/to/yugabyte-0.0.0.1-b1.tar.gz");
    when(configHelper.getConfig(SoftwareReleases))
        .thenReturn(ImmutableMap.of("0.0.0.1-b1", metadata));
    try {
      releaseManager.addReleaseWithMetadata("0.0.0.1-b1", metadata);
    } catch (RuntimeException re) {
      assertEquals("Release already exists for version 0.0.0.1-b1", re.getMessage());
    }
  }

  @Test
  public void testUpdateCurrentReleasesFromRaw() {
    Map<String, Object> myMap =
        new HashMap<String, Object>() {
          {
            put("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
            put("0.0.2", "/path/to/yugabyte-0.0.2-centos-aarch64.tar.gz");
          }
        };
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(myMap);
    releaseManager.updateCurrentReleases();
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
                .withFilePath("/path/to/yugabyte-0.0.1.tar.gz")
                .withChartPath(""),
            "0.0.2",
            ReleaseManager.ReleaseMetadata.create("0.0.2")
                .withFilePath("/path/to/yugabyte-0.0.2-centos-aarch64.tar.gz")
                .withChartPath("")
                .withPackage(
                    "/path/to/yugabyte-0.0.2-centos-aarch64.tar.gz", Architecture.aarch64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testUpdateCurrentReleasesFromCreate() {
    ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(ImmutableMap.of("0.0.1", metadata));
    releaseManager.updateCurrentReleases();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    Map expectedMap = ImmutableMap.of("0.0.1", ReleaseManager.ReleaseMetadata.create("0.0.1"));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testUpdateCurrentReleasesFromLegacy() {
    ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.fromLegacy("0.0.1", "/path/to/yugabyte-0.0.1.tar.gz");
    ReleaseMetadata metadataPackage =
        ReleaseManager.ReleaseMetadata.fromLegacy(
            "0.0.2", "/path/to/yugabyte-0.0.1-centos-x86_64.tar.gz");
    when(configHelper.getConfig(SoftwareReleases))
        .thenReturn(ImmutableMap.of("0.0.1", metadata, "0.0.2", metadataPackage));
    releaseManager.updateCurrentReleases();
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
                .withFilePath("/path/to/yugabyte-0.0.1.tar.gz")
                .withChartPath(""),
            "0.0.2",
            ReleaseManager.ReleaseMetadata.create("0.0.2")
                .withFilePath("/path/to/yugabyte-0.0.1-centos-x86_64.tar.gz")
                .withChartPath("")
                .withPackage("/path/to/yugabyte-0.0.1-centos-x86_64.tar.gz", Architecture.x86_64));
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testUpdateCurrentReleasesFromPackage() {
    Map expectedMap =
        ImmutableMap.of(
            "0.0.4",
            ReleaseManager.ReleaseMetadata.create("0.0.4")
                .withFilePath(TMP_STORAGE_PATH + "/0.0.4/yugabyte-0.0.4-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.4/yugabyte-0.0.4-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.4/yugabyte-0.0.4-centos-x86_64.tar.gz",
                    Architecture.x86_64)
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.4/yugabyte-0.0.4-centos-aarch64.tar.gz",
                    Architecture.aarch64));
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(expectedMap);
    releaseManager.updateCurrentReleases();
    ArgumentCaptor<ConfigHelper.ConfigType> configType;
    ArgumentCaptor<HashMap> releaseMap;
    configType = ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(1))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    assertReleases(expectedMap, releaseMap.getValue());
  }

  @Test
  public void testAddGFlagMetadataForCloudRelease() {
    ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    metadata.s3 = new ReleaseMetadata.S3Location();
    metadata.s3.paths = new ReleaseMetadata.PackagePaths();
    metadata.s3.paths.x86_64 = "s3://foo";
    metadata.s3.accessKeyId = "abc";
    metadata.s3.secretAccessKey = "abc";
    when(mockGFlagsValidation.getMissingGFlagFileList(any()))
        .thenReturn(GFlagsValidation.GFLAG_FILENAME_LIST);
    releaseManager.addGFlagsMetadataFiles("0.0.1", metadata);
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testFixFilePathsReleaseMetadata() {
    // Create s3 release that will not change.
    ReleaseMetadata metadata = ReleaseManager.ReleaseMetadata.create("0.0.1");
    metadata.s3 = new ReleaseMetadata.S3Location();
    metadata.s3.paths = new ReleaseMetadata.PackagePaths();
    metadata.s3.paths.x86_64 = "s3://foo";
    metadata.s3.accessKeyId = "abc";
    metadata.s3.secretAccessKey = "abc";
    Map<String, Object> temp = new HashMap<String, Object>();
    temp.put("0.0.1", metadata);
    when(configHelper.getConfig(SoftwareReleases)).thenReturn(temp);
    releaseManager.addGFlagsMetadataFiles("0.0.1", metadata);

    // Create dummy releases that will get imported.
    List<String> versions = ImmutableList.of("0.0.0.2-b1", "0.0.0.3-b2");
    createDummyReleases(versions, false, false, false, true, true);
    when(appConfig.getString("yb.releases.path"))
        .thenReturn(TMP_STORAGE_PATH)
        .thenReturn(TMP_REPLACE_STORAGE_PATH);
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.ybdbHelmReleasePathRegex))
        .thenReturn("[^.]+yugabyte-(.*)-helm.tar.gz");
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);
    releaseManager.importLocalReleases();

    // Create new release files for 0.0.0.2-b1.
    String version = "0.0.0.2-b1";
    String newVersionPath = String.format("%s/%s/", TMP_REPLACE_STORAGE_PATH, version);
    String fileFormat = "yugabyte-%s-%s.tar.gz";
    new File(newVersionPath).mkdirs();
    createTempFile(
        newVersionPath, String.format(fileFormat, version, "centos-x86_64"), "SampleRelease");
    createTempFile(
        newVersionPath, String.format(fileFormat, version, "centos-aarch64"), "SampleRelease");
    createTempFile(newVersionPath, String.format(fileFormat, version, "helm"), "SampleHelm");
    releaseManager.fixFilePaths();

    // Validate release metadata.
    Map expectedMap =
        ImmutableMap.of(
            "0.0.1",
            metadata,
            "0.0.0.2-b1",
            ReleaseManager.ReleaseMetadata.create("0.0.0.2-b1")
                .withFilePath(newVersionPath + String.format(fileFormat, version, "centos-x86_64"))
                .withChartPath(newVersionPath + String.format(fileFormat, version, "helm"))
                .withPackage(
                    newVersionPath + String.format(fileFormat, version, "centos-x86_64"),
                    Architecture.x86_64)
                .withPackage(
                    newVersionPath + String.format(fileFormat, version, "centos-aarch64"),
                    Architecture.aarch64),
            "0.0.0.3-b2",
            ReleaseManager.ReleaseMetadata.create("0.0.0.3-b2")
                .withFilePath(
                    TMP_STORAGE_PATH + "/0.0.0.3-b2/yugabyte-0.0.0.3-b2-centos-x86_64.tar.gz")
                .withChartPath(TMP_STORAGE_PATH + "/0.0.0.3-b2/yugabyte-0.0.0.3-b2-helm.tar.gz")
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.3-b2/yugabyte-0.0.0.3-b2-centos-x86_64.tar.gz",
                    Architecture.x86_64)
                .withPackage(
                    TMP_STORAGE_PATH + "/0.0.0.3-b2/yugabyte-0.0.0.3-b2-centos-aarch64.tar.gz",
                    Architecture.aarch64));
    ArgumentCaptor<ConfigHelper.ConfigType> configType =
        ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    ArgumentCaptor<HashMap> releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(3))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    List<HashMap> capturedReleases = releaseMap.getAllValues();
    assertReleases(expectedMap, capturedReleases.get(1));
  }

  @Test
  public void testFixFilePathsYbcMetadata() {
    // Create ybc metadata to update.
    String version = "1.0.0-b1";
    ReleaseMetadata metadata =
        ReleaseManager.ReleaseMetadata.create(version)
            .withFilePath(TMP_STORAGE_PATH + "/1.0.0-b1/ybc-1.0.0-b1-linux-x86_64.tar.gz")
            .withPackage(
                TMP_STORAGE_PATH + "/1.0.0-b1/ybc-1.0.0-b1-linux-x86_64.tar.gz",
                Architecture.x86_64);
    String version2 = "2.0.0-b2";
    ReleaseMetadata metadata2 =
        ReleaseManager.ReleaseMetadata.create(version2)
            .withFilePath(TMP_STORAGE_PATH + "/2.0.0-b2/ybc-2.0.0-b2-el8-aarch64.tar.gz")
            .withPackage(
                TMP_STORAGE_PATH + "/2.0.0-b2/ybc-2.0.0-b2-el8-aarch64.tar.gz",
                Architecture.aarch64);
    Map<String, Object> temp = new HashMap<String, Object>();
    temp.put("1.0.0-b1", metadata);
    temp.put("2.0.0-b2", metadata2);
    when(configHelper.getConfig(YbcSoftwareReleases)).thenReturn(temp);
    when(appConfig.getString("ybc.releases.path")).thenReturn(TMP_REPLACE_STORAGE_PATH);
    String newVersionPath = String.format("%s/%s/", TMP_REPLACE_STORAGE_PATH, version);
    new File(newVersionPath).mkdirs();
    createTempFile(newVersionPath, "ybc-1.0.0-b1-linux-x86_64.tar.gz", "ybcData");
    releaseManager.fixFilePaths();
    Map expectedMap =
        ImmutableMap.of(
            "1.0.0-b1",
            ReleaseManager.ReleaseMetadata.create(version)
                .withFilePath(
                    TMP_REPLACE_STORAGE_PATH + "/1.0.0-b1/ybc-1.0.0-b1-linux-x86_64.tar.gz")
                .withPackage(
                    TMP_REPLACE_STORAGE_PATH + "/1.0.0-b1/ybc-1.0.0-b1-linux-x86_64.tar.gz",
                    Architecture.x86_64),
            "2.0.0-b2",
            metadata2);
    ArgumentCaptor<ConfigHelper.ConfigType> configType =
        ArgumentCaptor.forClass(ConfigHelper.ConfigType.class);
    ArgumentCaptor<HashMap> releaseMap = ArgumentCaptor.forClass(HashMap.class);
    Mockito.verify(configHelper, times(2))
        .loadConfigToDB(configType.capture(), releaseMap.capture());
    List<HashMap> capturedReleases = releaseMap.getAllValues();
    assertReleases(expectedMap, capturedReleases.get(1));
  }
}
