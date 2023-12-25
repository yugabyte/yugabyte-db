// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.api.gax.paging.Page;
import com.google.auth.Credentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.Bucket;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.Storage.BucketListOption;
import com.google.cloud.storage.StorageBatch;
import com.google.cloud.storage.StorageBatchResult;
import com.google.cloud.storage.StorageException;
import com.google.cloud.storage.StorageOptions;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.UnsupportedEncodingException;
import java.nio.channels.Channels;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.StreamSupport;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class GCPUtil implements CloudUtil {

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";
  private static final String GS_PROTOCOL_PREFIX = "gs://";
  private static final String HTTPS_PROTOCOL_PREFIX = "https://storage.googleapis.com/";

  public static final String YBC_GOOGLE_APPLICATION_CREDENTIALS_FIELDNAME =
      "GOOGLE_APPLICATION_CREDENTIALS";

  public static String[] getSplitLocationValue(String location) {
    int prefixLength =
        location.startsWith(GS_PROTOCOL_PREFIX)
            ? GS_PROTOCOL_PREFIX.length()
            : (location.startsWith(HTTPS_PROTOCOL_PREFIX) ? HTTPS_PROTOCOL_PREFIX.length() : 0);

    location = location.substring(prefixLength);
    String[] split = location.split("/", 2);
    return split;
  }

  @Override
  public ConfigLocationInfo getConfigLocationInfo(String location) {
    String[] splitLocations = getSplitLocationValue(location);
    String bucket = splitLocations.length > 0 ? splitLocations[0] : "";
    String cloudPath = splitLocations.length > 1 ? splitLocations[1] : "";
    return new ConfigLocationInfo(bucket, cloudPath);
  }

  public static Storage getStorageService(CustomerConfigStorageGCSData gcsData)
      throws IOException, UnsupportedEncodingException {
    String gcsCredentials = gcsData.gcsCredentialsJson;
    Credentials credentials =
        GoogleCredentials.fromStream(new ByteArrayInputStream(gcsCredentials.getBytes("UTF-8")));
    Storage storage = StorageOptions.newBuilder().setCredentials(credentials).build().getService();
    return storage;
  }

  @Override
  public void checkStoragePrefixValidity(String configLocation, String backupLocation) {
    String[] configLocationSplit = getSplitLocationValue(configLocation);
    String[] backupLocationSplit = getSplitLocationValue(backupLocation);
    // Buckets should be same in any case.
    if (!StringUtils.equals(configLocationSplit[0], backupLocationSplit[0])) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          String.format(
              "Config bucket %s and backup location bucket %s do not match",
              configLocationSplit[0], backupLocationSplit[0]));
    }
  }

  @Override
  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception {
    String[] splitLocation = getSplitLocationValue(defaultBackupLocation);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try {
      Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
      Boolean deleted = storage.delete(bucketName, keyLocation);
      if (!deleted) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
        return;
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
      }
    } catch (StorageException e) {
      log.error("Error while deleting key object from bucket " + bucketName, e.getReason());
      throw e;
    }
  }

  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Collection<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    for (String configLocation : locations) {
      try {
        String[] splitLocation = getSplitLocationValue(configLocation);
        String bucketName = splitLocation.length > 0 ? splitLocation[0] : "";
        String prefix = splitLocation.length > 1 ? splitLocation[1] : "";
        Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
        if (splitLocation.length == 1) {
          storage.list(bucketName);
        } else {
          storage.list(
              bucketName,
              Storage.BlobListOption.prefix(prefix),
              Storage.BlobListOption.currentDirectory());
        }
      } catch (Exception e) {
        log.error(
            String.format(
                "GCP Credential cannot list objects in the specified backup location %s",
                configLocation),
            e);
        return false;
      }
    }
    return true;
  }

  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception {
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);

        List<StorageBatchResult<Boolean>> results = new ArrayList<>();
        StorageBatch storageBatch = storage.batch();
        try {
          Page<Blob> blobs = storage.list(bucketName, Storage.BlobListOption.prefix(objectPrefix));
          if (blobs != null) {
            log.debug(
                "Retrieved blobs info for bucket " + bucketName + " with prefix " + objectPrefix);
            StreamSupport.stream(blobs.iterateAll().spliterator(), true)
                .forEach(
                    blob -> {
                      results.add(storageBatch.delete(blob.getBlobId()));
                    });
          }
        } finally {
          if (!results.isEmpty()) {
            storageBatch.submit();
            if (!results.stream().allMatch(r -> r != null && r.get())) {
              throw new RuntimeException(
                  "Error in deleting objects in bucket "
                      + bucketName
                      + " with prefix "
                      + objectPrefix);
            }
          }
        }
      } catch (StorageException e) {
        log.error(" Error in deleting objects at location " + backupLocation, e.getReason());
        throw e;
      }
    }
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
    String[] splitLocation = getSplitLocationValue(cloudPath);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    Blob blob = storage.get(bucketName, objectPrefix);
    if (blob == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "No blob was found at the specified location: " + cloudPath);
    }
    return Channels.newInputStream(blob.reader());
  }

  /*
   * For GCS location like gs://bucket/suffix,
   * splitLocation[0] is equal to bucket
   * splitLocation[1] is equal to the suffix part of string
   */
  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    try {
      Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
      AtomicInteger count = new AtomicInteger(0);
      return locations.stream()
          .map(
              l -> {
                String[] splitLocation = getSplitLocationValue(l);
                String bucketName = splitLocation[0];

                // This is the absolute location inside the GS bucket to get the file
                String objectSuffix =
                    splitLocation.length > 1
                        ? BackupUtil.getPathWithPrefixSuffixJoin(splitLocation[1], fileName)
                        : fileName;
                Blob blob = storage.get(bucketName, objectSuffix);
                if (blob != null && blob.exists()) {
                  count.incrementAndGet();
                }
                return count;
              })
          .anyMatch(i -> checkExistsOnAll ? (i.get() == locations.size()) : (i.get() == 1));
    } catch (IOException e) {
      throw new RuntimeException("Error checking files on locations", e);
    }
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    String storageLocation = getRegionLocationsMap(configData).get(region);
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    String cloudDir =
        splitValues.length > 1
            ? BackupUtil.getPathWithPrefixSuffixJoin(splitValues[1], commonDir)
            : commonDir;
    cloudDir = BackupUtil.appendSlash(cloudDir);
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      splitValues = getSplitLocationValue(previousBackupLocation);
      previousCloudDir =
          splitValues.length > 1 ? BackupUtil.appendSlash(splitValues[1]) : previousCloudDir;
    }
    Map<String, String> gcsCredsMap = createCredsMapYbc(gcsData);
    return YbcBackupUtil.buildCloudStoreSpec(
        bucket, cloudDir, previousCloudDir, gcsCredsMap, Util.GCS);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    String storageLocation = getRegionLocationsMap(configData).get(region);
    String[] splitValues = getSplitLocationValue(storageLocation);
    String bucket = splitValues[0];
    Map<String, String> gcsCredsMap = createCredsMapYbc(gcsData);
    if (isDsm) {
      String location = BackupUtil.appendSlash(getSplitLocationValue(cloudDir)[1]);
      return YbcBackupUtil.buildCloudStoreSpec(bucket, location, "", gcsCredsMap, Util.GCS);
    }
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, "", gcsCredsMap, Util.GCS);
  }

  private Map<String, String> createCredsMapYbc(CustomerConfigData configData) {
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    Map<String, String> gcsCredsMap = new HashMap<>();
    gcsCredsMap.put(YBC_GOOGLE_APPLICATION_CREDENTIALS_FIELDNAME, gcsData.gcsCredentialsJson);
    return gcsCredsMap;
  }

  public List<String> listBuckets(CustomerConfigData configData) {
    List<String> bucketList = new ArrayList<>();
    try {
      CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
      if (StringUtils.isBlank(gcsData.gcsCredentialsJson)) {
        return bucketList;
      }
      Storage gcsClient = getStorageService(gcsData);
      BucketListOption options = BucketListOption.pageSize(100);
      Page<Bucket> buckets = gcsClient.list(options);
      Iterator<Bucket> bucketIterator = buckets.iterateAll().iterator();
      bucketIterator.forEachRemaining(bI -> bucketList.add(bI.getName()));
    } catch (StorageException e) {
      log.error("Error retrieving list of buckets");
    } catch (IOException e) {
      log.error("Error creating GCS client");
    }
    return bucketList;
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    if (CollectionUtils.isNotEmpty(gcsData.regionLocations)) {
      gcsData.regionLocations.stream()
          .forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    regionLocationsMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, gcsData.backupLocation);
    return regionLocationsMap;
  }
}
