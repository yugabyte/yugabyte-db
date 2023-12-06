// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.services;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.io.File;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.Base64;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;

@RunWith(MockitoJUnitRunner.class)
public class FileDataServiceTest extends FakeDBApplication {
  final String TMP_STORAGE_PATH = "/tmp/yugaware_tests/" + getClass().getSimpleName();
  final String TMP_REPLACE_STORAGE_PATH =
      "/tmp/yugaware_replace_tests/" + getClass().getSimpleName();
  final String TMP_KEYS_PATH = TMP_STORAGE_PATH + "/keys";
  final String TMP_REPLACE_KEYS_PATH = TMP_REPLACE_STORAGE_PATH + "/keys";
  final String[] KEY_FILES = {"public.key", "private.key", "vault.file", "vault.password"};

  FileDataService fileDataService;
  SettableRuntimeConfigFactory runtimeConfigFactory;
  Customer customer;

  @Override
  protected Application provideApplication() {
    return super.provideApplication(
        ImmutableMap.of(
            "yb.fs_stateless.max_files_count_persist",
            100,
            "yb.fs_stateless.suppress_error",
            true,
            "yb.fs_stateless.disable_sync_db_to_fs_startup",
            false,
            "yb.fs_stateless.max_file_size_bytes",
            10000));
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    fileDataService = app.injector().instanceOf(FileDataService.class);
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  @Before
  public void beforeTest() throws IOException {
    new File(TMP_STORAGE_PATH).mkdirs();
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testSyncFileData() throws IOException {
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.getUuid() + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    for (String diskFileName : diskFileNames) {
      String filePath = "/node-agent/certs/" + customer.getUuid() + "/" + UUID.randomUUID() + "/0/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);

    String[] dbFileNames = {"testFile4.txt", "testFile5", "testFile6.root.crt"};
    for (String dbFileName : dbFileNames) {
      UUID parentUUID = UUID.randomUUID();
      String filePath = "/keys/" + parentUUID + "/" + dbFileName;
      String content = Base64.getEncoder().encodeToString(UUID.randomUUID().toString().getBytes());
      FileData.create(parentUUID, filePath, dbFileName, content);
    }
    for (String dbFileName : dbFileNames) {
      UUID parentUUID = UUID.randomUUID();
      String filePath =
          "/node-agent/certs/" + customer.getUuid() + "/" + parentUUID + "/0/" + dbFileName;
      String content = Base64.getEncoder().encodeToString(UUID.randomUUID().toString().getBytes());
      FileData.create(parentUUID, filePath, dbFileName, content);
    }

    fileDataService.syncFileData(TMP_STORAGE_PATH, true);
    List<FileData> fd = FileData.getAll();
    assertEquals(fd.size(), 12);
    Collection<File> diskFiles = FileUtils.listFiles(new File(TMP_STORAGE_PATH), null, true);
    assertEquals(diskFiles.size(), 12);
    FileUtils.deleteDirectory(new File(TMP_STORAGE_PATH));
  }

  @Test
  public void testSyncFileDataWithSuppressException() throws IOException {
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.max_file_size_bytes", "0");
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.getUuid() + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);

    // No Exception should be thrown.
    List<FileData> fd = FileData.getAll();
    assertEquals(fd.size(), 0);

    runtimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.fs_stateless.max_file_size_bytes", "10000");
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);
    fd = FileData.getAll();
    assertEquals(fd.size(), 3);
  }

  @Test(expected = Exception.class)
  public void testSyncFileDataThrowException() throws IOException {
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.suppress_error", "false");
    runtimeConfigFactory.globalRuntimeConf().setValue("yb.fs_stateless.max_file_size_bytes", "0");
    Provider p = ModelFactory.awsProvider(customer);
    String[] diskFileNames = {"testFile1.txt", "testFile2", "testFile3.root.crt"};
    for (String diskFileName : diskFileNames) {
      String filePath = "/keys/" + p.getUuid() + "/";
      createTempFile(TMP_STORAGE_PATH + filePath, diskFileName, UUID.randomUUID().toString());
    }
    fileDataService.syncFileData(TMP_STORAGE_PATH, false);
  }

  private void createKeyFiles(String directory, String provisionDir) {
    for (String file : KEY_FILES) {
      createTempFile(directory, file, UUID.randomUUID().toString());
    }
    createTempFile(provisionDir, "provision_instance.py", UUID.randomUUID().toString());
  }

  private void fillKeyInfo(String directory, String provisionDir, AccessKey.KeyInfo key) {
    key.publicKey = directory + "public.key";
    key.privateKey = directory + "private.key";
    key.vaultFile = directory + "vault.file";
    key.vaultPasswordFile = directory + "vault.password";
    key.provisionInstanceScript = provisionDir + "provision_instance.py";
  }

  private void validateKeyInfo(AccessKey.KeyInfo expKeyInfo, AccessKey.KeyInfo repKeyInfo) {
    assertEquals(expKeyInfo.publicKey, repKeyInfo.publicKey);
    assertEquals(expKeyInfo.privateKey, repKeyInfo.privateKey);
    assertEquals(expKeyInfo.vaultFile, repKeyInfo.vaultFile);
    assertEquals(expKeyInfo.vaultPasswordFile, repKeyInfo.vaultPasswordFile);
    assertEquals(expKeyInfo.provisionInstanceScript, repKeyInfo.provisionInstanceScript);
  }

  @Test
  public void testFixFilePathsOnpremProvider() {
    Provider onpremProvider = ModelFactory.onpremProvider(customer);
    // Make access key files and objects.
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    AccessKey.KeyInfo expKeyInfo = new AccessKey.KeyInfo();
    String accessKeyDir = String.format("%s/%s/", TMP_KEYS_PATH, onpremProvider.getUuid());
    String accessKeyReplaceDir =
        String.format("%s/%s/", TMP_REPLACE_KEYS_PATH, onpremProvider.getUuid());
    String provisionScriptDir =
        String.format("%s/provision/%s/", TMP_STORAGE_PATH, onpremProvider.getUuid());
    String provisionReplaceScriptDir =
        String.format("%s/provision/%s/", TMP_REPLACE_STORAGE_PATH, onpremProvider.getUuid());
    createKeyFiles(accessKeyDir, provisionScriptDir);
    createKeyFiles(accessKeyReplaceDir, provisionReplaceScriptDir);
    fillKeyInfo(accessKeyDir, provisionScriptDir, keyInfo);
    fillKeyInfo(accessKeyReplaceDir, provisionReplaceScriptDir, expKeyInfo);
    AccessKey accessKey = AccessKey.create(onpremProvider.getUuid(), "key-onprem-replace", keyInfo);
    // Because of https://phabricator.dev.yugabyte.com/D21552 need to set at provider level.
    onpremProvider.getDetails().provisionInstanceScript =
        provisionScriptDir + "provision_instance.py";
    onpremProvider.getAllAccessKeys().add(accessKey);
    onpremProvider.save();

    // Fix up paths.
    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    onpremProvider.refresh();
    AccessKey.KeyInfo repKeyInfo = onpremProvider.getAllAccessKeys().get(0).getKeyInfo();
    validateKeyInfo(expKeyInfo, repKeyInfo);
    assertEquals(
        provisionReplaceScriptDir + "provision_instance.py",
        onpremProvider.getDetails().provisionInstanceScript);
  }

  @Test
  public void testFixFilePathsOnpremProviderEqual() {
    Provider onpremProvider = ModelFactory.onpremProvider(customer);
    // Make access key files and objects.
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    AccessKey.KeyInfo expKeyInfo = new AccessKey.KeyInfo();
    String accessKeyDir = String.format("%s/%s/", TMP_KEYS_PATH, onpremProvider.getUuid());
    String provisionScriptDir =
        String.format("%s/provision/%s/", TMP_STORAGE_PATH, onpremProvider.getUuid());
    createKeyFiles(accessKeyDir, provisionScriptDir);
    fillKeyInfo(accessKeyDir, provisionScriptDir, keyInfo);
    fillKeyInfo(accessKeyDir, provisionScriptDir, expKeyInfo);
    AccessKey accessKey = AccessKey.create(onpremProvider.getUuid(), "key-onprem-replace", keyInfo);
    // Because of https://phabricator.dev.yugabyte.com/D21552 need to set at provider level.
    onpremProvider.getDetails().provisionInstanceScript =
        provisionScriptDir + "provision_instance.py";
    onpremProvider.getAllAccessKeys().add(accessKey);
    onpremProvider.save();

    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    onpremProvider.refresh();
    AccessKey.KeyInfo repKeyInfo = onpremProvider.getAllAccessKeys().get(0).getKeyInfo();
    validateKeyInfo(expKeyInfo, repKeyInfo);
    assertEquals(
        provisionScriptDir + "provision_instance.py",
        onpremProvider.getDetails().provisionInstanceScript);
  }

  @Test
  public void testFixFilePathsGcpProvider() {
    Provider gcpProvider = ModelFactory.gcpProvider(customer);
    // Make gcp credentials json.
    GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(gcpProvider);
    gcpCloudInfo.setGceApplicationCredentialsPath(
        String.format("%s/%s/", TMP_KEYS_PATH, gcpProvider.getUuid()) + "credentials.json");
    createTempFile(
        String.format("%s/%s/", TMP_REPLACE_KEYS_PATH, gcpProvider.getUuid()),
        "credentials.json",
        UUID.randomUUID().toString());
    gcpProvider.save();

    // Replace credentials path.
    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    gcpProvider.refresh();
    GCPCloudInfo newGcpCloudInfo = CloudInfoInterface.get(gcpProvider);
    assertEquals(
        String.format("%s/%s/", TMP_REPLACE_KEYS_PATH, gcpProvider.getUuid()) + "credentials.json",
        newGcpCloudInfo.getGceApplicationCredentialsPath());
  }

  @Test
  public void testFixFilePathsKubernetesProvider() {
    Provider k8sProvider = ModelFactory.kubernetesProvider(customer);
    Region k8sRegion = Region.create(k8sProvider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone k8sZone =
        AvailabilityZone.createOrThrow(k8sRegion, "az-1", "PlacementAZ-1", "subnet-1");

    KubernetesInfo k8sProviderInfo = CloudInfoInterface.get(k8sProvider);
    KubernetesRegionInfo k8sRegionInfo = CloudInfoInterface.get(k8sRegion);
    KubernetesRegionInfo k8sZoneInfo = CloudInfoInterface.get(k8sZone);

    // Set kube config and pull secret at all three levels (provider/region/AZ).
    String kubeConfigPath =
        String.format("%s/%s/kubeconfig.conf", TMP_KEYS_PATH, k8sProvider.getUuid());
    k8sProviderInfo.setKubeConfig(kubeConfigPath);
    k8sRegionInfo.setKubeConfig(kubeConfigPath);
    k8sZoneInfo.setKubeConfig(kubeConfigPath);
    createTempFile(
        String.format("%s/%s/", TMP_REPLACE_KEYS_PATH, k8sProvider.getUuid()),
        "kubeconfig.conf",
        UUID.randomUUID().toString());
    String pullSecretPath =
        String.format("%s/%s/k8s-pull-secret.yml", TMP_KEYS_PATH, k8sProvider.getUuid());
    k8sProviderInfo.setKubernetesPullSecret(pullSecretPath);
    k8sRegionInfo.setKubernetesPullSecret(pullSecretPath);
    k8sZoneInfo.setKubernetesPullSecret(pullSecretPath);
    createTempFile(
        String.format("%s/%s/", TMP_REPLACE_KEYS_PATH, k8sProvider.getUuid()),
        "k8s-pull-secret.yml",
        UUID.randomUUID().toString());
    k8sProvider.save();
    k8sRegion.save();
    k8sZone.save();

    // Fix file paths and validate.
    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    k8sProvider.refresh();
    k8sRegion.refresh();
    k8sZone.refresh();
    k8sProviderInfo = CloudInfoInterface.get(k8sProvider);
    k8sRegionInfo = CloudInfoInterface.get(k8sProvider.getRegions().get(0));
    k8sZoneInfo = CloudInfoInterface.get(k8sProvider.getRegions().get(0).getZones().get(0));
    String newKubeConfigPath = kubeConfigPath.replace(TMP_KEYS_PATH, TMP_REPLACE_KEYS_PATH);
    assertEquals(newKubeConfigPath, k8sProviderInfo.getKubeConfig());
    assertEquals(newKubeConfigPath, k8sRegionInfo.getKubeConfig());
    assertEquals(newKubeConfigPath, k8sZoneInfo.getKubeConfig());
    String newPullSecretPath = pullSecretPath.replace(TMP_KEYS_PATH, TMP_REPLACE_KEYS_PATH);
    assertEquals(newPullSecretPath, k8sProviderInfo.getKubernetesPullSecret());
    assertEquals(newPullSecretPath, k8sRegionInfo.getKubernetesPullSecret());
    assertEquals(newPullSecretPath, k8sZoneInfo.getKubernetesPullSecret());
  }

  @Test
  public void testFixFilePathsCertificates() throws IOException, NoSuchAlgorithmException {
    // Create certificate info object.
    Date date = new Date();
    String certsDir = String.format("%s/certs/%s/", TMP_STORAGE_PATH, customer.getUuid());
    String newCertsDir = certsDir.replace(TMP_STORAGE_PATH, TMP_REPLACE_STORAGE_PATH);
    String privateKey = certsDir + "ca.key.pem";
    String certificate = certsDir + "ca.root.crt";
    createTempFile(certsDir, "ca.root.crt", UUID.randomUUID().toString());
    createTempFile(newCertsDir, "ca.key.pem", UUID.randomUUID().toString());
    createTempFile(newCertsDir, "ca.root.crt", UUID.randomUUID().toString());
    CertificateInfo cert =
        ModelFactory.createCertificateInfo(
            customer.getUuid(), certificate, CertConfigType.SelfSigned);
    cert.setPrivateKey(privateKey);
    cert.save();

    // Fix file paths and validate.
    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    cert.refresh();
    String newPrivateKey = privateKey.replace(TMP_STORAGE_PATH, TMP_REPLACE_STORAGE_PATH);
    String newCertificate = certificate.replace(TMP_STORAGE_PATH, TMP_REPLACE_STORAGE_PATH);
    assertEquals(newPrivateKey, cert.getPrivateKey());
    assertEquals(newCertificate, cert.getCertificate());
  }

  @Test
  public void testFixFilePathsLicenses() {
    String licensePath =
        String.format("%s/licenses/%s/license.txt", TMP_STORAGE_PATH, customer.getUuid());
    CustomerLicense license =
        CustomerLicense.create(customer.getUuid(), licensePath, "test_license_type");
    createTempFile(
        String.format("%s/licenses/%s/", TMP_REPLACE_STORAGE_PATH, customer.getUuid()),
        "license.txt",
        UUID.randomUUID().toString());
    license.save();

    fileDataService.fixUpPaths(TMP_REPLACE_STORAGE_PATH);
    license.refresh();
    assertEquals(
        licensePath.replace(TMP_STORAGE_PATH, TMP_REPLACE_STORAGE_PATH), license.getLicense());
  }
}
