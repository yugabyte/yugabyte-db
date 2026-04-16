package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ExtraMigrationManagerTest extends FakeDBApplication {
  public ExtraMigrationManager extraMigrationManager;
  @Mock ReleasesUtils mockReleasesUtils;

  @Before
  public void setUp() {
    extraMigrationManager = new ExtraMigrationManager();
    extraMigrationManager.templateManager = mockTemplateManager;
    extraMigrationManager.releaseManager = mockReleaseManager;
    extraMigrationManager.releasesUtils = mockReleasesUtils;

    when(mockReleaseManager.metadataFromObject(any())).thenCallRealMethod();
    when(mockReleasesUtils.releaseTypeFromVersion(any())).thenReturn("PREVIEW");
  }

  @Test
  public void testReleaseMetadataMigration() {
    Map<String, Object> releases = new HashMap<>();
    // Helm only release
    releases.put(
        "2.23.0.0-b1", createMetadata("2.23.0.0-b1", null, null, null, null, "/path/to/helm"));
    // Helm + s3 release
    releases.put(
        "2.23.0.0-b2",
        createMetadata(
            "2.23.0.0-b2",
            new PathArchHelper("s3://path", Architecture.x86_64),
            null,
            null,
            null,
            "/path/to/helm"));
    // helm + gcs release
    releases.put(
        "2.23.0.0-b3",
        createMetadata(
            "2.23.0.0-b3",
            null,
            new PathArchHelper("gcs://path", Architecture.aarch64),
            null,
            null,
            "/path/to/helm"));
    // Helm + local file
    releases.put(
        "2.23.0.0-b4",
        createMetadata(
            "2.23.0.0-b4",
            null,
            null,
            null,
            new PathArchHelper("/local/file/path", Architecture.x86_64),
            "/path/to/helm"));
    // Helm + local file with release already existing, but no helm chart attached
    releases.put(
        "2.23.0.0-b41",
        createMetadata(
            "2.23.0.0-b41",
            null,
            null,
            null,
            new PathArchHelper("/local/file/path", Architecture.x86_64),
            "/path/to/helm"));
    Release temp = Release.create("2.23.0.0-b41", "PREVIEW");
    temp.addArtifact(
        ReleaseArtifact.create(
            null,
            ReleaseArtifact.Platform.LINUX,
            Architecture.x86_64,
            ReleaseLocalFile.create("/local/file/path").getFileUUID()));
    // Helm + http
    releases.put(
        "2.23.0.0-b5",
        createMetadata(
            "2.23.0.0-b5",
            null,
            null,
            new PathArchHelper("https://yugabyte.com/local/file/path", Architecture.x86_64),
            null,
            "/path/to/helm"));
    // Local file only
    releases.put(
        "2.23.0.0-b6",
        createMetadata(
            "2.23.0.0-b6",
            null,
            null,
            null,
            new PathArchHelper("/local/file/path", Architecture.x86_64),
            null));
    releases.put(
        "2.23.0.0-b7",
        createMetadata(
            "2.23.0.0-b7",
            null,
            null,
            null,
            new PathArchHelper("/local/file/path", Architecture.x86_64),
            ""));
    // Http only
    releases.put(
        "2.23.0.0-b8",
        createMetadata(
            "2.23.0.0-b8",
            null,
            null,
            new PathArchHelper("http://yugabyte.com/local/file/path", Architecture.x86_64),
            null,
            ""));
    // 3 packages
    releases.put(
        "2.23.0.0-b9",
        createMetadata(
            "2.23.0.0-b9",
            null,
            null,
            new PathArchHelper("https://other.com/url", Architecture.x86_64),
            new PathArchHelper("/local/file/path", Architecture.aarch64),
            "/path/to/helm"));
    when(mockReleaseManager.getReleaseMetadata()).thenReturn(releases);
    extraMigrationManager.R__Release_Metadata_Migration();
    // Helm validation
    Release r1 = Release.getByVersion("2.23.0.0-b1");
    assertNotNull(r1);
    assertEquals(1, r1.getArtifacts().size());
    ReleaseArtifact ra = r1.getArtifacts().get(0);
    assertEquals(ReleaseArtifact.Platform.KUBERNETES, ra.getPlatform());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate helm + s3
    Release r2 = Release.getByVersion("2.23.0.0-b2");
    assertNotNull(r2);
    assertEquals(2, r2.getArtifacts().size());
    ra = r2.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getS3File());
    assertEquals("s3://path", ra.getS3File().path);
    ra = r2.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate gcs + helm
    Release r3 = Release.getByVersion("2.23.0.0-b3");
    assertNotNull(r3);
    assertEquals(2, r3.getArtifacts().size());
    ra = r3.getArtifactForArchitecture(Architecture.aarch64);
    assertNotNull(ra);
    assertNotNull(ra.getGcsFile());
    assertEquals("gcs://path", ra.getGcsFile().path);
    ra = r3.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate local file + helm
    Release r4 = Release.getByVersion("2.23.0.0-b4");
    assertNotNull(r4);
    assertEquals(2, r4.getArtifacts().size());
    ra = r4.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals(
        "/local/file/path", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
    ra = r4.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
    // Validate local file + helm with exisiting release
    Release r41 = Release.getByVersion("2.23.0.0-b41");
    assertNotNull(r41);
    assertEquals(2, r41.getArtifacts().size());
    ra = r41.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals(
        "/local/file/path", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
    ra = r41.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate http file + helm
    Release r5 = Release.getByVersion("2.23.0.0-b5");
    assertNotNull(r5);
    assertEquals(2, r5.getArtifacts().size());
    ra = r5.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageURL());
    assertEquals("https://yugabyte.com/local/file/path", ra.getPackageURL());
    ra = r5.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate Local file only
    Release r6 = Release.getByVersion("2.23.0.0-b6");
    assertNotNull(r6);
    assertEquals(1, r6.getArtifacts().size());
    ra = r6.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals(
        "/local/file/path", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
    Release r7 = Release.getByVersion("2.23.0.0-b7");
    assertNotNull(r7);
    assertEquals(1, r7.getArtifacts().size());
    ra = r6.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals(
        "/local/file/path", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());

    // Validate HTTP only
    Release r8 = Release.getByVersion("2.23.0.0-b8");
    assertNotNull(r8);
    assertEquals(1, r8.getArtifacts().size());
    ra = r8.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageURL());
    assertEquals("http://yugabyte.com/local/file/path", ra.getPackageURL());

    // Validate All Packages
    Release r9 = Release.getByVersion("2.23.0.0-b9");
    assertNotNull(r9);
    assertEquals(3, r9.getArtifacts().size());
    ra = r9.getArtifactForArchitecture(Architecture.x86_64);
    assertNotNull(ra);
    assertEquals("https://other.com/url", ra.getPackageURL());
    ra = r9.getArtifactForArchitecture(Architecture.aarch64);
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals(
        "/local/file/path", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
    ra = r9.getKubernetesArtifact();
    assertNotNull(ra);
    assertNotNull(ra.getPackageFileID());
    assertEquals("/path/to/helm", ReleaseLocalFile.get(ra.getPackageFileID()).getLocalFilePath());
  }

  public static class PathArchHelper {
    String path;
    Architecture arch;

    public PathArchHelper(String path, Architecture arch) {
      this.path = path;
      this.arch = arch;
    }
  }

  ReleaseMetadata createMetadata(
      String version,
      PathArchHelper s3,
      PathArchHelper gcs,
      PathArchHelper http,
      PathArchHelper file,
      String helmChart) {
    ReleaseMetadata metadata = ReleaseMetadata.create(version);
    metadata.imageTag = version;
    if (s3 != null) {
      metadata.withPackage(s3.path, s3.arch);
      metadata.filePath = s3.path;
      metadata.s3 = new ReleaseMetadata.S3Location();
      metadata.s3.accessKeyId = "accessKey";
      metadata.s3.secretAccessKey = "secretKey";
      metadata.s3.paths = new ReleaseMetadata.PackagePaths();
      metadata.s3.paths.x86_64 = s3.path;
    }
    if (gcs != null) {
      metadata.withPackage(gcs.path, gcs.arch);
      metadata.filePath = gcs.path;
      metadata.gcs = new ReleaseMetadata.GCSLocation();
      metadata.gcs.credentialsJson = "json_creds";
      metadata.gcs.paths = new ReleaseMetadata.PackagePaths();
      metadata.gcs.paths.x86_64 = gcs.path;
    }
    if (http != null) {
      metadata.withPackage(http.path, http.arch);
      metadata.filePath = http.path;
      metadata.http = new ReleaseMetadata.HttpLocation();
      metadata.http.paths = new ReleaseMetadata.PackagePaths();
      metadata.http.paths.x86_64 = http.path;
    }
    if (file != null) {
      metadata.withPackage(file.path, file.arch);
      metadata.filePath = file.path;
    }
    metadata.chartPath = helmChart;
    return metadata;
  }
}
