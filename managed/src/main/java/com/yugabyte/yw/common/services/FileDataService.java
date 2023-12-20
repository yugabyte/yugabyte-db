/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.services;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.CustomerLicense;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class FileDataService {

  private static final List<String> FILE_DIRECTORY_TO_SYNC =
      ImmutableList.of("keys", "certs", "licenses", "node-agent/certs");
  private static final String UUID_REGEX =
      "[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}";
  private static final Pattern keyPattern =
      Pattern.compile(String.format("(.*)(/keys/%s/)(.*)", UUID_REGEX));
  private static final Pattern provisionPattern =
      Pattern.compile(String.format("(.*)(/provision/%s/provision_instance.py)", UUID_REGEX));
  private static final Pattern certPattern =
      Pattern.compile(String.format("(.*)(/certs/%s/)(.*)", UUID_REGEX));
  private static final Pattern licensePattern =
      Pattern.compile(String.format("(.*)(/licenses/%s/)(.*)", UUID_REGEX));
  private static final Pattern nodeCertPattern =
      Pattern.compile(String.format("(.*)(/node-agent/certs/%s/%s/)(.*)", UUID_REGEX, UUID_REGEX));
  private final RuntimeConfGetter confGetter;
  private final ConfigHelper configHelper;

  @Inject
  public FileDataService(RuntimeConfGetter confGetter, ConfigHelper configHelper) {
    this.confGetter = confGetter;
    this.configHelper = configHelper;
  }

  public void syncFileData(String storagePath, Boolean ywFileDataSynced) {
    Boolean suppressExceptionsDuringSync =
        confGetter.getGlobalConf(GlobalConfKeys.fsStatelessSuppressError);
    int fileCountThreshold =
        confGetter.getGlobalConf(GlobalConfKeys.fsStatelessMaxFilesCountPersist);
    Boolean disableSyncDBStateToFS =
        confGetter.getGlobalConf(GlobalConfKeys.disableSyncDbToFsStartup);
    try {
      long fileSyncStartTime = System.currentTimeMillis();
      Collection<File> diskFiles = Collections.emptyList();
      List<FileData> dbFiles = FileData.getAll();
      int currentFileCountDB = dbFiles.size();

      for (String fileDirectoryName : FILE_DIRECTORY_TO_SYNC) {
        File ywDir = new File(storagePath + "/" + fileDirectoryName);
        if (ywDir.exists()) {
          Collection<File> diskFile = FileUtils.listFiles(ywDir, null, true);
          diskFiles =
              Stream.of(diskFiles, diskFile)
                  .flatMap(Collection::stream)
                  .collect(Collectors.toList());
        }
      }
      // List of files on disk with relative path to storage.
      Set<String> filesOnDisk =
          diskFiles.stream()
              .map(File::getAbsolutePath)
              .map(fileName -> fileName.replace(storagePath, ""))
              .collect(Collectors.toSet());
      Set<FileData> fileDataNames = FileData.getAllNames();
      Set<String> filesInDB =
          fileDataNames.stream().map(FileData::getRelativePath).collect(Collectors.toSet());

      if (!ywFileDataSynced) {
        Set<String> fileOnlyOnDisk = Sets.difference(filesOnDisk, filesInDB);
        if (currentFileCountDB + fileOnlyOnDisk.size() > fileCountThreshold) {
          // We fail in case the count exceeds the threshold.
          throw new RuntimeException(
              "The Maximum files count to be persisted in the DB exceeded the "
                  + "configuration. Update the flag `yb.fs_stateless.max_files_count_persist` "
                  + "to update the limit or try deleting some files");
        }

        // For all files only on disk, update them in the DB.
        for (String file : fileOnlyOnDisk) {
          log.info("Syncing file " + file + " to the DB");
          FileData.writeFileToDB(storagePath + file, storagePath, confGetter);
        }
        log.info("Successfully Written " + fileOnlyOnDisk.size() + " files to DB.");
      }

      if (!disableSyncDBStateToFS) {
        Set<String> fileOnlyInDB = Sets.difference(filesInDB, filesOnDisk);
        // For all files only in the DB, write them to disk.
        for (String file : fileOnlyInDB) {
          log.info("Syncing " + file + " file from the DB to FS");
          FileData.writeFileToDisk(FileData.getFromFile(file), storagePath);
        }
        log.info("Successfully Written " + fileOnlyInDB.size() + " files to disk.");
      }

      if (!ywFileDataSynced) {
        Map<String, Object> ywFileDataSync = new HashMap<>();
        ywFileDataSync.put("synced", "true");
        configHelper.loadConfigToDB(ConfigType.FileDataSync, ywFileDataSync);
      }
      long fileSyncEndTime = System.currentTimeMillis();
      log.debug("Time taken for file sync operation: {}", fileSyncEndTime - fileSyncStartTime);
    } catch (Exception e) {
      if (suppressExceptionsDuringSync) {
        log.error(e.getMessage());
      } else {
        throw e;
      }
    }
  }

  /**
   * This method returns a modified file path after replacing a prefix (usually representing the
   * storage path) with a new prefix. The replacement is done on the first captured group of the
   * pattern.
   *
   * @param pattern (Pattern): The pattern to match on oldPath
   * @param oldPath (str): The existing path to fix up with newPathPrefix
   * @param newPathPrefix (str): The new prefix to modify oldPath
   */
  public static String fixFilePath(Pattern pattern, String oldPath, String newPathPrefix) {
    if (StringUtils.isNotBlank(oldPath)) {
      Matcher fileMatcher = pattern.matcher(oldPath);
      log.info("Fixing oldPath {}", oldPath);
      if (fileMatcher.find()) {
        String newPath =
            CommonUtils.replaceBeginningPath(oldPath, fileMatcher.group(1), newPathPrefix);
        if (Files.exists(Paths.get(newPath))) {
          log.info("Replacing prefix {} with conf value {}", fileMatcher.group(1), newPathPrefix);
          return newPath;
        } else {
          throw new RuntimeException("Could not locate " + newPath);
        }
      }
    }
    return oldPath;
  }

  private void fixAccessKeys(String storagePath) {
    // Fix up AccessKeys.
    for (AccessKey ak : AccessKey.getAll()) {
      AccessKey.KeyInfo keyInfo = ak.getKeyInfo();
      log.info("Analyzing key {}", keyInfo.privateKey);
      try {
        keyInfo.publicKey = fixFilePath(keyPattern, keyInfo.publicKey, storagePath);
      } catch (Exception e) {
        log.warn(
            "Error \"{}\" replacing public key path {}. Skipping.",
            e.getMessage(),
            keyInfo.publicKey);
      }

      try {
        keyInfo.privateKey = fixFilePath(keyPattern, keyInfo.privateKey, storagePath);
      } catch (Exception e) {
        log.warn(
            "Error {} replacing private key path {}. Skipping.",
            e.getMessage(),
            keyInfo.privateKey);
      }

      try {
        keyInfo.vaultPasswordFile = fixFilePath(keyPattern, keyInfo.vaultPasswordFile, storagePath);
      } catch (Exception e) {
        log.warn(
            "Error {} replacing vault password file {}. Skipping.",
            e.getMessage(),
            keyInfo.vaultPasswordFile);
      }

      try {
        keyInfo.vaultFile = fixFilePath(keyPattern, keyInfo.vaultFile, storagePath);
      } catch (Exception e) {
        log.warn("Error {} replacing vault file {}. Skipping.", e.getMessage(), keyInfo.vaultFile);
      }

      try {
        keyInfo.provisionInstanceScript =
            fixFilePath(provisionPattern, keyInfo.provisionInstanceScript, storagePath);
      } catch (Exception e) {
        log.warn(
            "Error {} replacing access key provision script {}. Skipping.",
            e.getMessage(),
            keyInfo.provisionInstanceScript);
      }

      ak.setKeyInfo(keyInfo);
      ak.save();
    }
  }

  private void fixProviderConfig(String storagePath) {
    List<Provider> providerList = Provider.getAll();
    for (Provider provider : providerList) {

      ProviderDetails details = provider.getDetails();
      if (details != null) {
        // Fix up provider details provision instance script.
        if (provider.getCloudCode().equals(Common.CloudType.onprem)) {
          try {
            details.provisionInstanceScript =
                fixFilePath(provisionPattern, details.provisionInstanceScript, storagePath);
          } catch (Exception e) {
            log.warn(
                "Error {} replacing provider provision script {}. Skipping.",
                e.getMessage(),
                details.provisionInstanceScript);
          }
        }
        // Fix up GCP credentials path.
        if (provider.getCloudCode().equals(Common.CloudType.gcp)) {
          GCPCloudInfo gcpCloudInfo = CloudInfoInterface.get(provider);
          try {
            gcpCloudInfo.setGceApplicationCredentialsPath(
                fixFilePath(
                    keyPattern, gcpCloudInfo.getGceApplicationCredentialsPath(), storagePath));
          } catch (Exception e) {
            log.warn(
                "Error {} replacing GCP application credentials {}. Skipping.",
                e.getMessage(),
                gcpCloudInfo.getGceApplicationCredentialsPath());
          }
        }
        if (provider.getCloudCode().equals(Common.CloudType.kubernetes)) {
          // Fix up k8s pull secret.
          KubernetesInfo k8sInfo = CloudInfoInterface.get(provider);
          try {
            k8sInfo.setKubernetesPullSecret(
                fixFilePath(keyPattern, k8sInfo.getKubernetesPullSecret(), storagePath));
          } catch (Exception e) {
            log.warn(
                "Error {} replacing kubernetes pull secret {}. Skipping.",
                e.getMessage(),
                k8sInfo.getKubernetesPullSecret());
          }
          // Fix up k8s kube config.
          try {
            k8sInfo.setKubeConfig(fixFilePath(keyPattern, k8sInfo.getKubeConfig(), storagePath));
          } catch (Exception e) {
            log.warn(
                "Error {} replacing kubernetes config {}. Skipping.",
                e.getMessage(),
                k8sInfo.getKubeConfig());
          }
          for (Region region : Region.getByProvider(provider.getUuid())) {
            KubernetesRegionInfo k8sRegionInfo = CloudInfoInterface.get(region);
            try {
              k8sRegionInfo.setKubeConfig(
                  fixFilePath(keyPattern, k8sRegionInfo.getKubeConfig(), storagePath));
            } catch (Exception e) {
              log.warn(
                  "Error {} replacing Region kubernetes config {}. Skipping.",
                  e.getMessage(),
                  k8sRegionInfo.getKubeConfig());
            }
            try {
              k8sRegionInfo.setKubernetesPullSecret(
                  fixFilePath(keyPattern, k8sRegionInfo.getKubernetesPullSecret(), storagePath));
            } catch (Exception e) {
              log.warn(
                  "Error {} replacing Region kubernetes pull secret {}. Skipping.",
                  e.getMessage(),
                  k8sRegionInfo.getKubernetesPullSecret());
            }
            for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(region.getUuid())) {
              KubernetesRegionInfo kubernetesAzRegionInfo = CloudInfoInterface.get(az);
              try {
                kubernetesAzRegionInfo.setKubeConfig(
                    fixFilePath(keyPattern, kubernetesAzRegionInfo.getKubeConfig(), storagePath));
              } catch (Exception e) {
                log.warn(
                    "Error {} replacing AZ kubernetes config {}. Skipping.",
                    e.getMessage(),
                    kubernetesAzRegionInfo.getKubeConfig());
              }
              try {
                kubernetesAzRegionInfo.setKubernetesPullSecret(
                    fixFilePath(
                        keyPattern, kubernetesAzRegionInfo.getKubernetesPullSecret(), storagePath));
              } catch (Exception e) {
                log.warn(
                    "Error {} replacing AZ kubernetes pull secret {}. Skipping.",
                    e.getMessage(),
                    kubernetesAzRegionInfo.getKubernetesPullSecret());
              }
              az.save();
            }
            region.save();
          }
        }
      }
      provider.setDetails(details);
      provider.save();
    }
  }

  private void fixCertificateInfo(String storagePath) {
    List<CertificateInfo> certList = CertificateInfo.getAll();
    for (CertificateInfo cert : certList) {
      try {
        cert.setPrivateKey(fixFilePath(certPattern, cert.getPrivateKey(), storagePath));
      } catch (Exception e) {
        log.warn(
            "Error {} replacing certificate info private key {}. Skipping.",
            e.getMessage(),
            cert.getPrivateKey());
      }

      try {
        cert.setCertificate(fixFilePath(certPattern, cert.getCertificate(), storagePath));
      } catch (Exception e) {
        log.warn(
            "Error {} replacing certificate info certificate {}. Skipping.",
            e.getMessage(),
            cert.getCertificate());
      }

      cert.save();
    }
  }

  private void fixNodeAgentCerts(String storagePath) {
    Set<NodeAgent> nodeAgentSet = NodeAgent.getAll();
    for (NodeAgent nodeAgent : nodeAgentSet) {
      try {
        nodeAgent.updateCertDirPath(
            Paths.get(
                fixFilePath(nodeCertPattern, nodeAgent.getCertDirPath().toString(), storagePath)));
      } catch (Exception e) {
        log.warn(
            "Error {} replacing node agent certificate directory {}. Skipping.",
            e.getMessage(),
            nodeAgent.getCertDirPath().toString());
      }
    }
  }

  private void fixLicenses(String storagePath) {
    List<CustomerLicense> licenseList = CustomerLicense.find.query().where().findList();
    for (CustomerLicense license : licenseList) {
      try {
        license.setLicense(fixFilePath(licensePattern, license.getLicense(), storagePath));
      } catch (Exception e) {
        log.warn("Error {} replacing license {}. Skipping.", e.getMessage(), license.getLicense());
      }
      license.save();
    }
  }

  public void fixUpPaths(String storagePath) {
    log.info("Fixing up file paths.");
    fixAccessKeys(storagePath);
    fixProviderConfig(storagePath);
    fixCertificateInfo(storagePath);
    fixLicenses(storagePath);
    fixNodeAgentCerts(storagePath);
  }
}
