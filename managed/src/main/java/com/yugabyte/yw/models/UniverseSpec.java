// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import io.ebean.annotation.Transactional;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.GZIPOutputStream;
import lombok.Builder;
import lombok.Data;
import lombok.extern.jackson.Jacksonized;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveOutputStream;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
@Builder
@Jacksonized
@Data
public class UniverseSpec {

  private static final String PEM_PERMISSIONS = "r--------";
  private static final String PUB_PERMISSIONS = "r--------";
  private static final String DEFAULT_PERMISSIONS = "rw-r--r--";

  public Universe universe;

  // Contains Region, AvailabilityZone, and AccessKey entities.
  public Provider provider;

  public List<InstanceType> instanceTypes;

  public List<PriceComponent> priceComponents;

  public List<CertificateInfo> certificateInfoList;

  public List<NodeInstance> nodeInstances;

  public List<KmsConfig> kmsConfigs;

  public List<KmsHistory> kmsHistoryList;

  public List<Backup> backups;

  public List<Schedule> schedules;

  public List<CustomerConfig> customerConfigs;

  public Map<String, String> universeConfig;

  public PlatformPaths oldPlatformPaths;

  private ReleaseMetadata ybReleaseMetadata;

  private boolean skipReleases;

  public InputStream exportSpec() throws IOException {
    String specBasePath = this.oldPlatformPaths.storagePath + "/universe-specs/export";
    String specName = UniverseSpec.generateSpecName(true);
    String specJsonPath = specBasePath + "/" + specName + ".json";
    String specTarGZPath = specBasePath + "/" + specName + ".tar.gz";
    Files.createDirectories(Paths.get(specBasePath));

    File jsonSpecFile = new File(specJsonPath);
    exportUniverseSpecObj(jsonSpecFile);

    try (FileOutputStream fos = new FileOutputStream(specTarGZPath);
        GZIPOutputStream gos = new GZIPOutputStream(new BufferedOutputStream(fos));
        TarArchiveOutputStream tarOS = new TarArchiveOutputStream(gos)) {
      tarOS.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);

      // Save universe spec.
      Util.copyFileToTarGZ(jsonSpecFile, "universe-spec.json", tarOS);

      // Save access key files.
      for (AccessKey accessKey : this.provider.getAllAccessKeys()) {
        exportAccessKey(accessKey, tarOS);
      }
      if (this.provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
        exportKubernetesAccessKey(tarOS);
      }

      // Save certificate files.
      for (CertificateInfo certificateInfo : certificateInfoList) {
        exportCertificateInfo(certificateInfo, tarOS);
      }

      // Save provivision script for on-prem provider.
      exportProvisionInstanceScript(tarOS);

      // Save ybc and software release files.
      if (!this.skipReleases) {
        exportYBSoftwareReleases(tarOS);
        exportYbcReleases(tarOS);
      }
    }

    Files.deleteIfExists(jsonSpecFile.toPath());
    InputStream is =
        FileUtils.getInputStreamOrFail(new File(specTarGZPath), true /* deleteOnClose */);
    return is;
  }

  public ObjectNode generateUniverseSpecObj() {
    ObjectNode universeSpecObj = (ObjectNode) Json.toJson(this);
    universeSpecObj = setIgnoredJsonProperties(universeSpecObj);
    return universeSpecObj;
  }

  private void exportUniverseSpecObj(File jsonSpecFile) throws IOException {
    ObjectMapper mapper = Json.mapper();
    ObjectNode universeSpecObj = generateUniverseSpecObj();
    log.debug("Finished serializing universeSpec object.");
    mapper.writeValue(jsonSpecFile, universeSpecObj);
  }

  private void exportAccessKey(AccessKey accessKey, TarArchiveOutputStream tarArchive)
      throws IOException {
    // VaultFile should always have a path.
    String vaultPath = accessKey.getKeyInfo().vaultFile;
    File vaultFile = new File(vaultPath);
    File accessKeyFolder = vaultFile.getParentFile();
    Util.addFilesToTarGZ(accessKeyFolder.getAbsolutePath(), "keys/", tarArchive);
    log.debug("Added accessKey {} to tar gz file.", accessKey.getKeyCode());
  }

  private void exportKubernetesAccessKey(TarArchiveOutputStream tarArchive) throws IOException {
    KubernetesInfo kubernetesInfo = this.provider.getDetails().getCloudInfo().getKubernetes();
    String kubernetesPullSecretPath = kubernetesInfo.getKubernetesPullSecret();
    File kubernetesPullSecretFile = new File(kubernetesPullSecretPath);
    File accessKeyFolder = kubernetesPullSecretFile.getParentFile();
    Util.addFilesToTarGZ(accessKeyFolder.getAbsolutePath(), "keys/", tarArchive);
    log.debug("Added kubernetes accessKey {} to tar gz file.", kubernetesPullSecretFile.getName());
  }

  // Certs are stored under {yb.storage.path}/certs/{customer_uuid}/{certificate_info_uuid}/
  // We will store each certificate folder in tar gz under certs/ without customer_uuid for
  // simplicity.
  // Legacy yugabytedb.crt/yugabytedb.pem files will be included as they are stored under
  //  certificateFolder (usually in n2n rootCA folder).
  private void exportCertificateInfo(
      CertificateInfo certificateInfo, TarArchiveOutputStream tarArchive) throws IOException {
    String certificatePath = certificateInfo.getCertificate();
    File certificateFile = new File(certificatePath);
    File certificateFolder = certificateFile.getParentFile();
    Util.addFilesToTarGZ(certificateFolder.getAbsolutePath(), "certs/", tarArchive);
    log.debug("Added certificate {} to tar gz file.", certificateInfo.getLabel());
  }

  private void exportProvisionInstanceScript(TarArchiveOutputStream tarArchive) throws IOException {
    if (this.provider.getCloudCode().equals(Common.CloudType.onprem)) {
      String provisionScriptPath = this.provider.getDetails().provisionInstanceScript;
      File provisionScriptFile = new File(provisionScriptPath);
      File provisionScriptFolder = provisionScriptFile.getParentFile();

      // Without this, untar will fail, as the folder will not exist.
      tarArchive.putArchiveEntry(
          tarArchive.createArchiveEntry(provisionScriptFolder, "provision/"));
      tarArchive.closeArchiveEntry();

      Util.addFilesToTarGZ(provisionScriptFile.getAbsolutePath(), "provision/", tarArchive);
    }
  }

  private void exportYBSoftwareReleases(TarArchiveOutputStream tarArchive) throws IOException {
    String universeVersion =
        this.universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    File releaseFolder =
        new File(String.format("%s/%s", this.oldPlatformPaths.releasesPath, universeVersion));
    Util.addFilesToTarGZ(releaseFolder.getAbsolutePath(), "releases/", tarArchive);
    log.debug("Added software release {} to tar gz file.", universeVersion);
  }

  private void exportYbcReleases(TarArchiveOutputStream tarArchive) throws IOException {
    File ybcReleaseFolder = new File(this.oldPlatformPaths.ybcReleasePath);
    String universeYbcVersion = this.universe.getUniverseDetails().getYbcSoftwareVersion();
    if (this.universe.getUniverseDetails().isYbcInstalled() && ybcReleaseFolder.isDirectory()) {
      File[] ybcTarFiles = ybcReleaseFolder.listFiles();
      Pattern ybcVersionPattern =
          Pattern.compile(String.format("%s-", Pattern.quote(universeYbcVersion)));
      if (ybcTarFiles != null) {
        // Need to add folder to tar gz file, otherwise, there are issues with untar.
        tarArchive.putArchiveEntry(tarArchive.createArchiveEntry(ybcReleaseFolder, "ybcRelease/"));
        tarArchive.closeArchiveEntry();

        for (File ybcTarFile : ybcTarFiles) {
          Matcher matcher = ybcVersionPattern.matcher(ybcTarFile.getName());
          boolean matchFound = matcher.find();
          if (matchFound) {
            Util.addFilesToTarGZ(ybcTarFile.getAbsolutePath(), "ybcRelease/", tarArchive);
          }
        }
      }
      log.debug("Added ybc release {} to tar gz file.", universeYbcVersion);
    }
  }

  // Retrieve unmasked details from provider, region, and availability zone.
  public ObjectNode setIgnoredJsonProperties(ObjectNode universeSpecObj) {
    ProviderDetails unmaskedProviderDetails = this.provider.getDetails();
    JsonNode unmaskedProviderDetailsJson = Json.toJson(unmaskedProviderDetails);
    ObjectNode providerObj = (ObjectNode) universeSpecObj.get("provider");
    providerObj.set("details", unmaskedProviderDetailsJson);

    List<Region> regions = this.provider.getRegions();
    ArrayNode regionsObj = (ArrayNode) providerObj.get("regions");
    if (regionsObj.isArray()) {
      for (int i = 0; i < regions.size(); i++) {
        ObjectNode regionObj = (ObjectNode) regionsObj.get(i);
        Region region = regions.get(i);
        RegionDetails unmaskedRegionDetails = region.getDetails();
        JsonNode unmaskedRegionDetailsJson = Json.toJson(unmaskedRegionDetails);
        regionObj.set("details", unmaskedRegionDetailsJson);

        List<AvailabilityZone> zones = region.getZones();
        ArrayNode zonesObj = (ArrayNode) regionObj.get("zones");
        if (zonesObj.isArray()) {
          for (int j = 0; j < zones.size(); j++) {
            ObjectNode zoneObj = (ObjectNode) zonesObj.get(j);
            AvailabilityZone zone = zones.get(j);
            AvailabilityZoneDetails unmaskedAZDetails = zone.getAvailabilityZoneDetails();
            JsonNode unmaskedAZDetailsJson = Json.toJson(unmaskedAZDetails);
            zoneObj.set("details", unmaskedAZDetailsJson);
          }
        }
      }
    }
    return universeSpecObj;
  }

  public static UniverseSpec importSpec(
      Path tarFile, PlatformPaths platformPaths, Customer customer) throws IOException {

    String storagePath = platformPaths.storagePath;
    String releasesPath = platformPaths.releasesPath;
    String ybcReleasePath = platformPaths.ybcReleasePath;
    String ybcReleasesPath = platformPaths.ybcReleasesPath;

    String specBasePath = storagePath + "/universe-specs/import";
    String specName = UniverseSpec.generateSpecName(false);
    String specFolderPath = specBasePath + "/" + specName;
    Files.createDirectories(Paths.get(specFolderPath));

    Util.extractFilesFromTarGZ(tarFile, specFolderPath);

    // Retrieve universe spec.
    UniverseSpec universeSpec = UniverseSpec.importUniverseSpec(specFolderPath);

    // Copy access keys to correct location if existing provider does not exist.
    universeSpec.importAccessKeys(specFolderPath, storagePath);

    // Copy certificate files to correct location.
    universeSpec.importCertificateInfoList(specFolderPath, storagePath, customer.getUuid());

    // Copy provision script for on-prem providers.
    universeSpec.importProvisionInstanceScript(specFolderPath, storagePath);

    if (!universeSpec.skipReleases) {
      // Import the ybsoftwareversions, etc if exists.
      universeSpec.importSoftwareReleases(specFolderPath, releasesPath);

      // Import the ybcsoftware version, etc if exists.
      universeSpec.importYbcSoftwareReleases(specFolderPath, ybcReleasePath);
      universeSpec.createYbcReleasesFolder(ybcReleasesPath);
    }

    // Update universe related metadata due to platform switch.
    // Placed after copying files due to dependencies upon files existing in correct location.
    universeSpec.updateUniverseMetadata(storagePath, customer);

    return universeSpec;
  }

  private static UniverseSpec importUniverseSpec(String specFolderPath) throws IOException {
    File jsonDir = new File(specFolderPath + "/universe-spec.json");
    ObjectMapper mapper = Json.mapper();
    UniverseSpec universeSpec = mapper.readValue(jsonDir, UniverseSpec.class);
    log.debug("Finished deserializing universe spec.");
    return universeSpec;
  }

  private void importAccessKeys(String specFolderPath, String storagePath) throws IOException {
    if (!Provider.maybeGet(provider.getUuid()).isPresent()) {
      File srcKeyMasterDir = new File(specFolderPath + "/keys");
      File[] keyDirs = srcKeyMasterDir.listFiles();
      if (keyDirs != null) {
        for (File keyDir : keyDirs) {
          File[] keyFiles = keyDir.listFiles();
          if (keyFiles != null) {
            for (File keyFile : keyFiles) {
              // K8s universes have kubeconfig accessKey on per az level, which will be a directory.
              if (!keyFile.isDirectory()) {
                String extension = FilenameUtils.getExtension(keyFile.getName());
                Set<PosixFilePermission> permissions =
                    PosixFilePermissions.fromString(DEFAULT_PERMISSIONS);
                if (extension.equals("pem")) {
                  permissions = PosixFilePermissions.fromString(PEM_PERMISSIONS);
                } else if (extension.equals("pub")) {
                  permissions = PosixFilePermissions.fromString(PUB_PERMISSIONS);
                }
                Files.setPosixFilePermissions(keyFile.toPath(), permissions);
              }
            }
          }
          log.debug("Saved access key folder {}.", keyDir.getName());
        }
      }
      File destKeyMasterDir = new File(storagePath + "/keys");
      org.apache.commons.io.FileUtils.copyDirectory(srcKeyMasterDir, destKeyMasterDir);
      log.debug("Saved access key file to {}", destKeyMasterDir.getPath());
    }
  }

  private void importCertificateInfoList(
      String specFolderPath, String storagePath, UUID customerUUID) throws IOException {
    File srcCertificateInfoMasterDir = new File(specFolderPath + "/certs");
    File[] certificateInfoDirs = srcCertificateInfoMasterDir.listFiles();

    if (certificateInfoDirs != null) {
      for (File certificateInfoDir : certificateInfoDirs) {
        String certificateInfoUUID = certificateInfoDir.getName();
        String certificateInfoBasePath =
            CertificateHelper.getCADirPath(
                storagePath, customerUUID, UUID.fromString(certificateInfoUUID));
        log.debug("Current certificate directory {}", certificateInfoBasePath);
        Files.createDirectories(Paths.get(certificateInfoBasePath));

        File[] certificateInfoFiles = certificateInfoDir.listFiles();
        if (certificateInfoFiles != null) {
          for (File certificateInfoFile : certificateInfoFiles) {
            Set<PosixFilePermission> permissions =
                PosixFilePermissions.fromString(DEFAULT_PERMISSIONS);
            Files.setPosixFilePermissions(certificateInfoFile.toPath(), permissions);
          }
        }

        File destCertificateInfoDir = new File(certificateInfoBasePath);
        org.apache.commons.io.FileUtils.copyDirectory(certificateInfoDir, destCertificateInfoDir);
        log.debug("Save certificate info folder {}.", certificateInfoUUID);
      }
    }
  }

  public void importProvisionInstanceScript(String specFolderPath, String storagePath)
      throws IOException {
    if (this.provider.getCloudCode().equals(Common.CloudType.onprem)) {
      File srcProvisionDir = new File(String.format("%s/provision", specFolderPath));
      File destReleasesDir =
          new File(
              String.format("%s/provision/%s", storagePath, this.provider.getUuid().toString()));
      org.apache.commons.io.FileUtils.copyDirectory(srcProvisionDir, destReleasesDir);
      log.debug("Finished importing provision instance script.");
    }
  }

  private void importSoftwareReleases(String specFolderPath, String releasesPath)
      throws IOException {
    String universeVersion =
        this.universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    File srcReleasesDir =
        new File(String.format("%s/releases/%s", specFolderPath, universeVersion));

    if (srcReleasesDir.isDirectory()) {
      File destReleasesDir = new File(String.format("%s/%s", releasesPath, universeVersion));
      Files.createDirectories(Paths.get(releasesPath));
      org.apache.commons.io.FileUtils.copyDirectory(srcReleasesDir, destReleasesDir);
      log.debug("Finished importing software release {}.", universeVersion);
    }
  }

  private void importYbcSoftwareReleases(String specFolderPath, String ybcReleasePath)
      throws IOException {
    File srcYbcReleaseDir = new File(String.format("%s/%s", specFolderPath, "ybcRelease"));
    File destYbcReleaseDir = new File(ybcReleasePath);
    if (srcYbcReleaseDir.isDirectory()) {
      org.apache.commons.io.FileUtils.copyDirectory(srcYbcReleaseDir, destYbcReleaseDir);
      log.debug("Finished importing ybc software release.");
    }
  }

  private void createYbcReleasesFolder(String ybcReleasesPath) throws IOException {
    if (!(new File(ybcReleasesPath)).isDirectory()) {
      Files.createDirectories(Paths.get(ybcReleasesPath));
      log.debug("Created ybc releases folder as it was not found.");
    }
  }

  private void updateUniverseDetails(Customer customer) {
    Long customerId = customer.getId();
    this.universe.setCustomerId(customerId);

    this.universe.setConfig(this.universeConfig);
  }

  private void updateProviderDetails(String storagePath, Customer customer) {
    // Use new customer.
    provider.setCustomerUUID(customer.getUuid());

    switch (this.provider.getCloudCode()) {
      case kubernetes:
        // Update abs. path for kubernetesPullSecret to use new yb.storage.path.
        KubernetesInfo kubernetesInfo = this.provider.getDetails().getCloudInfo().getKubernetes();
        kubernetesInfo.setKubernetesPullSecret(
            CommonUtils.replaceBeginningPath(
                kubernetesInfo.getKubernetesPullSecret(),
                this.oldPlatformPaths.storagePath,
                storagePath));

        // Update abs. path for kubeConfig to use new yb.storage.path.
        for (Region region : Region.getByProvider(this.provider.getUuid())) {
          for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(region.getUuid())) {
            KubernetesRegionInfo kubernetesRegionInfo =
                az.getAvailabilityZoneDetails().getCloudInfo().getKubernetes();
            kubernetesRegionInfo.setKubeConfig(
                CommonUtils.replaceBeginningPath(
                    kubernetesRegionInfo.getKubeConfig(),
                    this.oldPlatformPaths.storagePath,
                    storagePath));
          }
        }
        break;
      case gcp:
        // Update abs. path for credentials.json to use new yb.storage.path.
        GCPCloudInfo gcpCloudInfo = this.provider.getDetails().getCloudInfo().getGcp();
        gcpCloudInfo.setGceApplicationCredentialsPath(
            CommonUtils.replaceBeginningPath(
                gcpCloudInfo.getGceApplicationCredentialsPath(),
                this.oldPlatformPaths.storagePath,
                storagePath));
        break;
      case onprem:
        this.provider.getDetails().provisionInstanceScript =
            CommonUtils.replaceBeginningPath(
                this.provider.getDetails().provisionInstanceScript,
                this.oldPlatformPaths.storagePath,
                storagePath);
        break;
      default:
        break;
    }
  }

  private void updateCertificateInfoDetails(String storagePath, Customer customer) {
    UUID customerUUID = customer.getUuid();
    for (CertificateInfo certificateInfo : this.certificateInfoList) {
      UUID oldCustomerUUID = certificateInfo.getCustomerUUID();
      certificateInfo.setCustomerUUID(customerUUID);

      // HashicorpVault does not have privateKey.
      if (!StringUtils.isEmpty(certificateInfo.getPrivateKey())) {
        certificateInfo.setPrivateKey(
            CommonUtils.replaceBeginningPath(
                certificateInfo.getPrivateKey(), this.oldPlatformPaths.storagePath, storagePath));
        certificateInfo.setPrivateKey(
            certificateInfo
                .getPrivateKey()
                .replaceFirst(oldCustomerUUID.toString(), customerUUID.toString()));
      }

      certificateInfo.setCertificate(
          CommonUtils.replaceBeginningPath(
              certificateInfo.getCertificate(), this.oldPlatformPaths.storagePath, storagePath));
      certificateInfo.setCertificate(
          certificateInfo
              .getCertificate()
              .replaceFirst(oldCustomerUUID.toString(), customerUUID.toString()));
    }
  }

  // Update absolute path to access keys due to yb.storage.path changes.
  private void updateAccessKeyDetails(String storagePath) {
    for (AccessKey key : this.provider.getAllAccessKeys()) {
      if (key.getKeyInfo().publicKey != null) {
        key.getKeyInfo().publicKey =
            CommonUtils.replaceBeginningPath(
                key.getKeyInfo().publicKey, this.oldPlatformPaths.storagePath, storagePath);
      }
      if (key.getKeyInfo().privateKey != null) {
        key.getKeyInfo().privateKey =
            CommonUtils.replaceBeginningPath(
                key.getKeyInfo().privateKey, this.oldPlatformPaths.storagePath, storagePath);
      }
      key.getKeyInfo().vaultPasswordFile =
          CommonUtils.replaceBeginningPath(
              key.getKeyInfo().vaultPasswordFile, this.oldPlatformPaths.storagePath, storagePath);
      key.getKeyInfo().vaultFile =
          CommonUtils.replaceBeginningPath(
              key.getKeyInfo().vaultFile, this.oldPlatformPaths.storagePath, storagePath);
    }
  }

  private void updateKmsConfigDetails(Customer customer) {
    for (KmsConfig kmsConfig : this.kmsConfigs) {
      kmsConfig.setCustomerUUID(customer.getUuid());
    }
  }

  private void updateBackupDetails(Customer customer) {
    for (Backup backup : this.backups) {
      backup.setCustomerUUID(customer.getUuid());
      backup.getBackupInfo().customerUuid = customer.getUuid();
    }
  }

  private void updateScheduleDetails(Customer customer) {
    for (Schedule schedule : this.schedules) {
      schedule.setCustomerUUID(customer.getUuid());
    }
  }

  private void updateCustomerConfigDetails(Customer customer) {
    for (CustomerConfig customerConfig : this.customerConfigs) {
      customerConfig.setCustomerUUID(customer.getUuid());
    }
  }

  private void updateUniverseMetadata(String storagePath, Customer customer) {

    // Update universe information with new customer information and universe config.
    updateUniverseDetails(customer);

    // Update provider information with new customer information and k8s specific file paths.
    updateProviderDetails(storagePath, customer);

    // Update certificate file paths.
    updateCertificateInfoDetails(storagePath, customer);

    // Update access key file paths.
    updateAccessKeyDetails(storagePath);

    updateKmsConfigDetails(customer);

    updateBackupDetails(customer);

    updateScheduleDetails(customer);

    updateCustomerConfigDetails(customer);
  }

  @Transactional
  public void save(
      PlatformPaths platformPaths, ReleaseManager releaseManager, SwamperHelper swamperHelper) {

    // Check if provider exists, if not, save all entities related to provider (Region,
    // AvailabilityZone, AccessKey).
    if (!Provider.maybeGet(this.provider.getUuid()).isPresent()) {
      this.provider.save();

      if (this.provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
        try {
          // Kubernetes provider contains kubernetesPullSecret file.
          FileData.upsertFileInDB(
              this.provider.getDetails().getCloudInfo().getKubernetes().getKubernetesPullSecret());

          // Each az contains its own kubeConfig.
          for (Region region : Region.getByProvider(this.provider.getUuid())) {
            for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(region.getUuid())) {
              FileData.upsertFileInDB(
                  az.getAvailabilityZoneDetails().getCloudInfo().getKubernetes().getKubeConfig());
            }
          }
        } catch (IOException e) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format("Failed to write kubernetes config file in DB. %S", e.getMessage()));
        }
      }

      for (InstanceType instanceType : this.instanceTypes) {
        instanceType.save();
      }

      for (PriceComponent priceComponent : this.priceComponents) {
        priceComponent.save();
      }

      // Sync access keys to db.
      for (AccessKey key : this.provider.getAllAccessKeys()) {
        AccessKey.KeyInfo keyInfo = key.getKeyInfo();
        try {
          FileData.upsertFileInDB(keyInfo.vaultFile);
          FileData.upsertFileInDB(keyInfo.vaultPasswordFile);
          if (keyInfo.privateKey != null) {
            FileData.upsertFileInDB(keyInfo.privateKey);
          }
          if (keyInfo.publicKey != null) {
            FileData.upsertFileInDB(keyInfo.publicKey);
          }

          File accessKeyDir = new File(keyInfo.vaultFile).getParentFile();
          // GCP provider contains credentials.json file.
          if (this.provider.getCloudCode().equals(Common.CloudType.gcp)) {
            FileData.upsertFileInDB(
                Paths.get(accessKeyDir.getAbsolutePath(), "credentials.json").toString());
          }
        } catch (IOException e) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format("Failed to write access key file in DB. %s", e.getMessage()));
        }
      }
    }

    for (CertificateInfo certificateInfo : certificateInfoList) {
      if (!CertificateInfo.maybeGet(certificateInfo.getUuid()).isPresent()) {
        certificateInfo.save();

        File certificateInfoBaseDir =
            new File(
                CertificateHelper.getCADirPath(
                    platformPaths.storagePath,
                    certificateInfo.getCustomerUUID(),
                    certificateInfo.getUuid()));
        File[] certificateInfoFiles = certificateInfoBaseDir.listFiles();
        if (certificateInfoFiles != null) {
          for (File certificateInfoFile : certificateInfoFiles) {
            try {
              FileData.upsertFileInDB(certificateInfoFile.getAbsolutePath());
            } catch (Exception e) {
              throw new PlatformServiceException(
                  INTERNAL_SERVER_ERROR,
                  String.format("Failed to write certificates in DB. %s", e.getMessage()));
            }
          }
        }
      }
    }

    for (NodeInstance nodeInstance : this.nodeInstances) {
      if (!NodeInstance.maybeGetByName(nodeInstance.getNodeName()).isPresent()) {
        nodeInstance.save();
      }
    }

    for (KmsConfig kmsConfig : kmsConfigs) {
      if (KmsConfig.get(kmsConfig.getConfigUUID()) == null) {
        kmsConfig.save();
      }
    }

    for (KmsHistory kmsHistory : kmsHistoryList) {
      if (!EncryptionAtRestUtil.keyRefExists(
          this.universe.getUniverseUUID(), kmsHistory.getUuid().keyRef)) {
        kmsHistory.save();
      }
    }

    for (CustomerConfig customerConfig : customerConfigs) {
      if (CustomerConfig.get(customerConfig.getConfigUUID()) == null) {
        customerConfig.save();
      }
    }

    for (Schedule schedule : this.schedules) {
      if (!Schedule.maybeGet(schedule.getScheduleUUID()).isPresent()) {
        schedule.save();
      }
    }

    for (Backup backup : this.backups) {
      if (!Backup.maybeGet(backup.getBackupUUID()).isPresent()) {
        backup.save();
      }
    }

    if (!this.skipReleases) {
      // Update and save software releases and ybc software releases.
      if (ybReleaseMetadata != null) {
        String universeVersion =
            this.universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
        if (releaseManager.getReleaseByVersion(universeVersion) == null) {
          releaseManager.addReleaseWithMetadata(universeVersion, ybReleaseMetadata);
        } else {
          releaseManager.updateReleaseMetadata(universeVersion, ybReleaseMetadata);
        }
      }

      // Imports local ybc and software releases.
      releaseManager.importLocalReleases();
      releaseManager.updateCurrentReleases();
    }

    // Unlock universe and save.
    UniverseDefinitionTaskParams universeDetails = this.universe.getUniverseDetails();
    universeDetails.updateInProgress = false;
    universeDetails.updateSucceeded = true;
    this.universe.setUniverseDetails(universeDetails);
    this.universe.save();

    // Update prometheus files.
    swamperHelper.writeUniverseTargetJson(this.universe);
  }

  public static String generateSpecName(boolean isExport) {
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String type = isExport ? "export" : "import";
    String specName = "yb-universe-spec-" + type + "-" + datePrefix;
    return specName;
  }

  @Builder
  @Jacksonized
  @Data
  public static class PlatformPaths {

    @JsonProperty("storagePath")
    public String storagePath;

    @JsonProperty("releasesPath")
    public String releasesPath;

    @JsonProperty("ybcReleasePath")
    public String ybcReleasePath;

    @JsonProperty("ybcReleasesPath")
    public String ybcReleasesPath;
  }
}
