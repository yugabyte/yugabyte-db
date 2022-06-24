// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.gax.paging.Page;
import com.google.auth.Credentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.StorageBatch;
import com.google.cloud.storage.StorageBatchResult;
import com.google.cloud.storage.StorageOptions;
import com.google.inject.Singleton;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.StreamSupport;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class GCPUtil {

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";
  private static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;
  private static final String GS_PROTOCOL_PREFIX = "gs://";
  private static final String HTTPS_PROTOCOL_PREFIX = "https://storage.googleapis.com/";

  public static String[] getSplitLocationValue(String location) {
    int prefixLength =
        location.startsWith(GS_PROTOCOL_PREFIX)
            ? GS_PROTOCOL_PREFIX.length()
            : (location.startsWith(HTTPS_PROTOCOL_PREFIX) ? HTTPS_PROTOCOL_PREFIX.length() : 0);

    location = location.substring(prefixLength);
    String[] split = location.split("/", 2);
    return split;
  }

  public static Storage getStorageService(String gcpCredentials)
      throws IOException, UnsupportedEncodingException {
    Credentials credentials =
        GoogleCredentials.fromStream(new ByteArrayInputStream(gcpCredentials.getBytes("UTF-8")));
    Storage storage = StorageOptions.newBuilder().setCredentials(credentials).build().getService();
    return storage;
  }

  public static void deleteKeyIfExists(JsonNode data, String backupLocation) throws Exception {
    String[] splitLocation = getSplitLocationValue(backupLocation);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try {
      String gcpCredentials = data.get(GCS_CREDENTIALS_JSON_FIELDNAME).asText();
      Storage storage = getStorageService(gcpCredentials);
      Boolean deleted = storage.delete(bucketName, keyLocation);
      if (!deleted) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
        return;
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
      }
    } catch (Exception e) {
      log.error("Error while deleting key object from bucket " + bucketName, e);
      throw e;
    }
  }

  public static Boolean canCredentialListObjects(JsonNode configData, List<String> locations) {
    for (String configLocation : locations) {
      try {
        String[] splitLocation = getSplitLocationValue(configLocation);
        String bucketName = splitLocation.length > 0 ? splitLocation[0] : "";
        String prefix = splitLocation.length > 1 ? splitLocation[1] : "";
        String gcpCredentials = configData.get(GCS_CREDENTIALS_JSON_FIELDNAME).asText();
        Storage storage = getStorageService(gcpCredentials);
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

  public static void deleteStorage(JsonNode data, List<String> backupLocations) throws Exception {
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        String gcpCredentials = data.get(GCS_CREDENTIALS_JSON_FIELDNAME).asText();
        Storage storage = getStorageService(gcpCredentials);

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
      } catch (Exception e) {
        log.error(" Error in deleting objects at location " + backupLocation, e);
        throw e;
      }
    }
  }
}
