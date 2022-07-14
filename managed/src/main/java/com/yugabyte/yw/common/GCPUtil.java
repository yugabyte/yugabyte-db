// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.api.gax.paging.Page;
import com.google.auth.Credentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.BlobId;
import com.google.cloud.storage.Bucket;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageBatch;
import com.google.cloud.storage.StorageBatchResult;
import com.google.cloud.storage.StorageException;
import com.google.cloud.storage.StorageOptions;
import com.google.inject.Inject;
import com.google.cloud.storage.Storage.BucketListOption;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Iterator;
import java.util.Spliterator;
import java.util.StringJoiner;
import java.util.stream.StreamSupport;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang.StringUtils;
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

  public static Storage getStorageService(CustomerConfigStorageGCSData gcsData)
      throws IOException, UnsupportedEncodingException {
    String gcsCredentials = gcsData.gcsCredentialsJson;
    Credentials credentials =
        GoogleCredentials.fromStream(new ByteArrayInputStream(gcsCredentials.getBytes("UTF-8")));
    Storage storage = StorageOptions.newBuilder().setCredentials(credentials).build().getService();
    return storage;
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

  public boolean canCredentialListObjects(CustomerConfigData configData, List<String> locations) {
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
  public CloudStoreSpec createCloudStoreSpec(
      String backupLocation, String commonDir, CustomerConfigData configData) {
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    String[] splitValues = getSplitLocationValue(backupLocation);
    String bucket = splitValues[0];
    String cloudDir =
        splitValues.length > 1
            ? String.format("%s/%s/", splitValues[1], commonDir)
            : commonDir + "/";
    Map<String, String> gcsCredsMap = createCredsMapYbc(gcsData);
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, gcsCredsMap, Util.GCS);
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
      gcsData
          .regionLocations
          .stream()
          .forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    return regionLocationsMap;
  }
}
