package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleasesUtils.ExtractedMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@Slf4j
@RunWith(MockitoJUnitRunner.class)
public class ReleasesUtilsTest extends FakeDBApplication {
  @InjectMocks ReleasesUtils releasesUtils;

  @Mock ConfigHelper configHelper;
  @Mock RuntimeConfGetter confGetter;
  @Mock Config appConfig;

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  @Test
  public void testVersionMetadataFromUrl() {
    URL url = getMockUrl("good_version_metadata.tgz");
    when(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion))
        .thenReturn(getVersionMap("2024.1.0.0-b23"));
    when(confGetter.getGlobalConf(GlobalConfKeys.skipYbaMinVersionCheck)).thenReturn(true);
    ExtractedMetadata em = releasesUtils.versionMetadataFromURL(url);
    assertEquals(Architecture.x86_64, em.architecture);
    assertEquals("2.0.0.0-b1", em.minimumYbaVersion);
  }

  @Test
  public void testMinimumYbaVersionFails() {
    URL url = getMockUrl("min_yba_version_fail.tgz");
    when(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion))
        .thenReturn(getVersionMap("2.1.0.0-b1"));
    when(confGetter.getGlobalConf(GlobalConfKeys.skipYbaMinVersionCheck)).thenReturn(false);
    assertThrows(PlatformServiceException.class, () -> releasesUtils.versionMetadataFromURL(url));
  }

  @Test
  public void testNoMinimumYbaVersionDefined() {
    URL url = getMockUrl("no_min_yba_metadata.tgz");
    ExtractedMetadata em = releasesUtils.versionMetadataFromURL(url);
    assertEquals(Architecture.aarch64, em.architecture);
    assertEquals("2.21.1.0-b180", em.version);
  }

  @Test
  public void testReleaseTypeFromVersion() {
    // Test 2.x vesions
    assertEquals("LTS", releasesUtils.releaseTypeFromVersion("2.14.1.0-b123"));
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2.16.3.0-b13"));
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2.16"));
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2.18.7.0-b23"));
    assertEquals("LTS", releasesUtils.releaseTypeFromVersion("2.20.0.0-b1"));
    assertEquals("PREVIEW", releasesUtils.releaseTypeFromVersion("2.17.1.0-b123"));
    assertEquals("PREVIEW", releasesUtils.releaseTypeFromVersion("2.19.1.0-b123"));
    assertEquals("PREVIEW", releasesUtils.releaseTypeFromVersion("2.21.1.0-b123"));
    assertEquals("PREVIEW", releasesUtils.releaseTypeFromVersion("2.23.1.0-b123"));

    // Test 2024 values
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2024.1.0.0-b3"));
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2024.1.2.0-b3"));
    assertEquals("STS", releasesUtils.releaseTypeFromVersion("2024.1.2.3-b3"));
    assertEquals("LTS", releasesUtils.releaseTypeFromVersion("2024.2.0.0-b3"));
  }

  @Test
  public void testValidateVersionAgainstCurrentYBA() {
    when(configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion))
        .thenReturn(getVersionMap("2024.1.0.0-b23"));
    when(confGetter.getGlobalConf(GlobalConfKeys.skipVersionChecks)).thenReturn(false);
    when(confGetter.getGlobalConf(GlobalConfKeys.allowDbVersionMoreThanYbaVersion))
        .thenReturn(false);

    // Should pass
    releasesUtils.validateVersionAgainstCurrentYBA("2024.1.0.0-b23");
    releasesUtils.validateVersionAgainstCurrentYBA("2024.1.0.0-b22");
    releasesUtils.validateVersionAgainstCurrentYBA("2024.0.0.0-b23");
    releasesUtils.validateVersionAgainstCurrentYBA("2024.0.0.0-b24");
    releasesUtils.validateVersionAgainstCurrentYBA("2.20.1.2-b99");

    // Should fail
    assertThrows(
        PlatformServiceException.class,
        () -> releasesUtils.validateVersionAgainstCurrentYBA("2024.1.0.0-b100"));
    assertThrows(
        PlatformServiceException.class,
        () -> releasesUtils.validateVersionAgainstCurrentYBA("2024.2.0.0-b1"));
    assertThrows(
        PlatformServiceException.class,
        () -> releasesUtils.validateVersionAgainstCurrentYBA("2025.1.0.0-b1"));
  }

  // PLAT-22914: continuous backup restore downloads every release to releases/<version>/, but the
  // restored DB still tracks uploaded releases under upload/release_artifacts/<uuid>/.
  @Test
  public void testRestoredUploadPathFixup() throws IOException {
    String storagePath = tmp.getRoot().getAbsolutePath();
    String releasesPath = tmp.newFolder("releases").getAbsolutePath();
    when(appConfig.getString(releasesUtils.STORAGE_PATH_CONFKEY)).thenReturn(storagePath);
    when(appConfig.getString(releasesUtils.RELEASE_PATH_CONFKEY))
        .thenReturn("upload/release_artifacts");
    when(appConfig.getString(Util.YB_RELEASES_PATH)).thenReturn(releasesPath);

    String version = "2025.2.6.0-b111";
    String fileName = "yugabyte-" + version + "-el8-aarch64.tar.gz";
    UUID fileUUID = UUID.randomUUID();
    Path uploadPath =
        Paths.get(storagePath, "upload/release_artifacts", fileUUID.toString(), fileName);
    ReleaseLocalFile.create(fileUUID, uploadPath.toString(), true);
    Release release = Release.create(version, "LTS");
    release.addArtifact(
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.aarch64, fileUUID));
    Path restoredPath = Files.createDirectories(Paths.get(releasesPath, version)).resolve(fileName);
    Files.writeString(restoredPath, "release");

    releasesUtils.restoredUploadPathFixup();

    assertTrue(Files.exists(Paths.get(ReleaseLocalFile.get(fileUUID).getLocalFilePath())));
    assertEquals(uploadPath.toString(), ReleaseLocalFile.get(fileUUID).getLocalFilePath());
    // A copy left in releases/ would be registered again by importLocalReleases.
    assertFalse(Files.exists(restoredPath));
  }

  private URL getMockUrl(String filename) {
    try {
      final URLConnection mockConnection = mock(URLConnection.class);
      when(mockConnection.getInputStream())
          .thenReturn(this.getClass().getResourceAsStream(filename));

      final URLStreamHandler handler =
          new URLStreamHandler() {
            @Override
            protected URLConnection openConnection(final URL arg0) throws IOException {
              return mockConnection;
            }
          };
      final URL url = new URL("http://foo.bar", "foo.bar", 80, "", handler);
      return url;
    } catch (IOException e) {
      throw new RuntimeException("failed to create a mock url", e);
    }
  }

  private Map<String, Object> getVersionMap(String version) {
    Map<String, Object> map = new HashMap<String, Object>();
    map.put("version", version);
    return map;
  }
}
