package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.ReleaseArtifact.GCSFile;
import com.yugabyte.yw.models.ReleaseArtifact.S3File;
import java.util.UUID;
import org.junit.Test;

public class ReleaseArtifactsTest extends FakeDBApplication {

  @Test
  public void testCreateURLArtifact() {
    ReleaseArtifact artifact =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.KUBERNETES, null, "https://url.com");
    ReleaseArtifact found = ReleaseArtifact.get(artifact.getArtifactUUID());
    assertEquals(artifact.getArtifactUUID(), found.getArtifactUUID());
    assertEquals(artifact.getPackageURL(), found.getPackageURL());
  }

  @Test
  public void testCreateFileIDArtifact() {
    String path = "/test/path";
    ReleaseLocalFile rlf = ReleaseLocalFile.create(path);
    ReleaseArtifact artifact =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.KUBERNETES, null, rlf.getFileUUID());
    ReleaseArtifact found = ReleaseArtifact.get(artifact.getArtifactUUID());
    assertEquals(artifact.getArtifactUUID(), found.getArtifactUUID());
    assertEquals(artifact.getPackageFileID(), found.getPackageFileID());
    assertEquals(ReleaseLocalFile.get(found.getPackageFileID()).getLocalFilePath(), path);
  }

  @Test
  public void testCreateS3Artifact() {
    S3File s3File = new S3File();
    s3File.path = "path";
    s3File.accessKeyId = "accessID";
    s3File.secretAccessKey = "secret";
    ReleaseArtifact artifact =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, s3File);
    ReleaseArtifact found = ReleaseArtifact.get(artifact.getArtifactUUID());
    assertEquals(artifact.getArtifactUUID(), found.getArtifactUUID());
    assertEquals(artifact.getS3File().path, found.getS3File().path);
    assertEquals(artifact.getS3File().accessKeyId, found.getS3File().accessKeyId);
    assertEquals(artifact.getS3File().secretAccessKey, found.getS3File().secretAccessKey);
  }

  @Test
  public void testCreateGCSArtifact() {
    GCSFile gcsFile = new GCSFile();
    gcsFile.path = "path";
    gcsFile.credentialsJson = "this is a json I promise";
    ReleaseArtifact artifact =
        ReleaseArtifact.create("sha256", ReleaseArtifact.Platform.KUBERNETES, null, gcsFile);
    ReleaseArtifact found = ReleaseArtifact.get(artifact.getArtifactUUID());
    String expectedPath = artifact.getGcsFile().path;
    String expectedJson = artifact.getGcsFile().credentialsJson;
    assertEquals(artifact.getArtifactUUID(), found.getArtifactUUID());
    assertEquals(expectedPath, found.getGcsFile().path);
    assertEquals(expectedJson, found.getGcsFile().credentialsJson);
  }

  @Test
  public void testGetKubernetesForRelease() {
    UUID releaseUUID = UUID.randomUUID();
    Release.create(releaseUUID, "version", "lts");
    UUID rlfUUID = UUID.randomUUID();
    ReleaseLocalFile.create(rlfUUID, "path", false);
    ReleaseArtifact artifact =
        ReleaseArtifact.create("", ReleaseArtifact.Platform.KUBERNETES, null, rlfUUID);
    artifact.saveReleaseUUID(releaseUUID);
    ReleaseArtifact foundArtifact = ReleaseArtifact.getForReleaseKubernetesArtifact(releaseUUID);
    assertNotNull(foundArtifact);
    assertEquals(artifact.getArtifactUUID(), foundArtifact.getArtifactUUID());
  }

  @Test
  public void testGetForX86Architecture() {
    UUID releaseUUID = UUID.randomUUID();
    Release.create(releaseUUID, "version", "lts");
    ReleaseArtifact artifact =
        ReleaseArtifact.create("", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "url");
    artifact.saveReleaseUUID(releaseUUID);
    ReleaseArtifact foundArtifact =
        ReleaseArtifact.getForReleaseArchitecture(releaseUUID, Architecture.x86_64);
    assertNotNull(foundArtifact);
    assertEquals(artifact.getArtifactUUID(), foundArtifact.getArtifactUUID());
  }

  @Test
  public void testGetForArchitectureKubernetes() {
    UUID releaseUUID = UUID.randomUUID();
    Release.create(releaseUUID, "version", "lts");
    ReleaseArtifact artifact =
        ReleaseArtifact.create("", ReleaseArtifact.Platform.KUBERNETES, null, "url");
    artifact.saveReleaseUUID(releaseUUID);
    ReleaseArtifact foundArtifact = ReleaseArtifact.getForReleaseArchitecture(releaseUUID, null);
    assertNotNull(foundArtifact);
    assertEquals(artifact.getArtifactUUID(), foundArtifact.getArtifactUUID());
  }

  @Test
  public void testSha1md5sum() {
    ReleaseArtifact shaArtifact =
        ReleaseArtifact.create(
            "SHA1:a98a128bd452373c75e8922d7b6de9f43616a44c",
            ReleaseArtifact.Platform.LINUX,
            Architecture.x86_64,
            "https://url1.com");
    ReleaseArtifact md5Artifact =
        ReleaseArtifact.create(
            "md5:a9fa98a22ff0a63cbd32dbde8df3aee7",
            ReleaseArtifact.Platform.LINUX,
            Architecture.aarch64,
            "https://url1.com");

    assertNull(shaArtifact.getFormattedSha256());
    assertEquals("SHA1:a98a128bd452373c75e8922d7b6de9f43616a44c", shaArtifact.getSha256());
    assertNull(md5Artifact.getFormattedSha256());
    assertEquals("md5:a9fa98a22ff0a63cbd32dbde8df3aee7", md5Artifact.getSha256());
  }

  @Test
  public void testNullSha() {
    ReleaseArtifact ra =
        ReleaseArtifact.create(null, ReleaseArtifact.Platform.KUBERNETES, null, "https;//url.com");
    assertNull(ra.getFormattedSha256());
  }
}
