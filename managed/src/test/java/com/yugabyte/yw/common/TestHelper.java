// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Maps;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Universe;
import io.ebean.DB;
import io.ebean.Database;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorOutputStream;
import org.slf4j.LoggerFactory;
import play.test.Helpers;

public class TestHelper {
  public static String TMP_PATH = "/tmp/yugaware_tests";

  public static String createTempFile(String basePath, String fileName, String data) {
    FileWriter fw;
    try {
      String filePath = TMP_PATH;
      if (basePath != null) {
        filePath = basePath;
      }
      File tmpFile = new File(filePath, fileName);
      tmpFile.getParentFile().mkdirs();
      fw = new FileWriter(tmpFile);
      fw.write(data);
      fw.close();
      tmpFile.deleteOnExit();
      return tmpFile.getAbsolutePath();
    } catch (IOException ex) {
      return null;
    }
  }

  public static String createTempFile(String fileName, String data) {
    return createTempFile(null, fileName, data);
  }

  public static String createTempFile(String data) {
    String fileName = UUID.randomUUID().toString();
    return createTempFile(fileName, data);
  }

  public static List<ILoggingEvent> captureLogEventsFor(Class<PlatformReplicationManager> clazz) {
    // get Logback Logger
    Logger prmLogger = (Logger) LoggerFactory.getLogger(clazz);

    // create and start a ListAppender
    ListAppender<ILoggingEvent> listAppender = new ListAppender<>();
    listAppender.start();
    prmLogger.addAppender(listAppender);
    return listAppender.list;
  }

  public static Map<String, Object> testDatabase() {
    return Maps.newHashMap(
        // Needed because we're using 'value' as column name. This makes H2 be happy with that./
        // PostgreSQL works fine with 'value' column as is.
        Helpers.inMemoryDatabase("default", ImmutableMap.of("NON_KEYWORDS", "VALUE")));
  }

  public static void shutdownDatabase() {
    Database server = DB.byName("default");
    server.shutdown(false, false);
    Database perfAdvisorServer = DB.byName("perf_advisor");
    perfAdvisorServer.shutdown(false, false);
  }

  public static void createTarGzipFiles(List<Path> paths, Path output) throws IOException {

    try (OutputStream fOut = Files.newOutputStream(output);
        BufferedOutputStream buffOut = new BufferedOutputStream(fOut);
        GzipCompressorOutputStream gzOut = new GzipCompressorOutputStream(buffOut);
        TarArchiveOutputStream tOut = new TarArchiveOutputStream(gzOut)) {

      for (Path path : paths) {

        if (!Files.isRegularFile(path)) {
          throw new IOException("Support only file!");
        }

        TarArchiveEntry tarEntry =
            new TarArchiveEntry(path.toFile(), path.getFileName().toString());

        tOut.putArchiveEntry(tarEntry);

        // copy file to TarArchiveOutputStream
        Files.copy(path, tOut);

        tOut.closeArchiveEntry();
      }

      tOut.finish();
    }
  }

  public static ObjectNode getFakeKmsAuthConfig(KeyProvider keyProvider) {
    ObjectNode fakeAuthConfig = new ObjectMapper().createObjectNode();
    switch (keyProvider) {
      case AWS:
        fakeAuthConfig.put("name", "fake-aws-kms-config");
        fakeAuthConfig.put("AWS_ACCESS_KEY_ID", "fake-access-key");
        fakeAuthConfig.put("AWS_SECRET_ACCESS_KEY", "fake-secret-access");
        fakeAuthConfig.put("AWS_KMS_ENDPOINT", "fake-kms-endpoint");
        fakeAuthConfig.put("cmk_policy", "fake-cmk-policy");
        fakeAuthConfig.put("AWS_REGION", "us-west-1");
        fakeAuthConfig.put("cmk_id", "fake-cmk-id");
        break;
      case GCP:
        fakeAuthConfig.put("name", "fake-gcp-kms-config");
        fakeAuthConfig.put("LOCATION_ID", "global");
        fakeAuthConfig.put("PROTECTION_LEVEL", "HSM");
        fakeAuthConfig.put("GCP_KMS_ENDPOINT", "fake-kms-endpoint");
        fakeAuthConfig.put("KEY_RING_ID", "yb-kr");
        fakeAuthConfig.put("CRYPTO_KEY_ID", "yb-ck");

        // Populate GCP config
        ObjectNode fakeGcpConfig = fakeAuthConfig.putObject("GCP_CONFIG");
        fakeGcpConfig.put("type", "service_account");
        fakeGcpConfig.put("project_id", "yugabyte");
        break;
      case AZU:
        fakeAuthConfig.put("name", "fake-azu-kms-config");
        fakeAuthConfig.put("CLIENT_ID", "fake-client-id");
        fakeAuthConfig.put("CLIENT_SECRET", "fake-client-secret");
        fakeAuthConfig.put("TENANT_ID", "fake-tenant-id");
        fakeAuthConfig.put("AZU_VAULT_URL", "fake-vault-url");
        fakeAuthConfig.put("AZU_KEY_NAME", "fake-key-name");
        fakeAuthConfig.put("AZU_KEY_ALGORITHM", "RSA");
        fakeAuthConfig.put("AZU_KEY_SIZE", 2048);
        break;
      case HASHICORP:
        fakeAuthConfig.put("name", "fake-hc-kms-config");
        fakeAuthConfig.put("HC_VAULT_ADDRESS", "fake-vault-address");
        fakeAuthConfig.put("HC_VAULT_TOKEN", "fake-vault-token");
        fakeAuthConfig.put("HC_VAULT_MOUNT_PATH", "fake-mount-path");
        fakeAuthConfig.put("HC_VAULT_ENGINE", "fake-vault-engine");
        break;
      case SMARTKEY:
        break;
    }

    return fakeAuthConfig;
  }

  public static void updateUniverseVersion(Universe universe, String version) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = version;
    details.upsertPrimaryCluster(userIntent, null);
    universe.setUniverseDetails(details);
    universe.save();
  }

  public static void updateUniverseSoftwareUpgradeState(
      Universe universe, SoftwareUpgradeState state) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.softwareUpgradeState = state;
    universe.setUniverseDetails(details);
    universe.save();
  }

  public static void updateUniverseIsRollbackAllowed(Universe universe, boolean isRollbackAllowed) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.isSoftwareRollbackAllowed = isRollbackAllowed;
    universe.setUniverseDetails(details);
    universe.save();
  }

  public static void updateUniversePrevSoftwareConfig(
      Universe universe, UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.prevYBSoftwareConfig = ybSoftwareConfig;
    universe.setUniverseDetails(details);
    universe.save();
  }

  public static void updateUniverseSystemdDetails(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.useSystemd = true;
    details.upsertPrimaryCluster(userIntent, null);
    universe.setUniverseDetails(details);
    universe.save();
  }
}
