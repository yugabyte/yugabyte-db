// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.common.Util;
import java.time.Duration;
import java.util.Iterator;
import java.util.List;
import java.util.ArrayList;
import lombok.extern.slf4j.Slf4j;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobClientBuilder;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.BackupTableParams;
import com.azure.storage.blob.models.DeleteSnapshotsOptionType;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;
import com.azure.storage.blob.models.BlobItem;
import com.azure.core.http.rest.PagedIterable;
import com.azure.core.http.rest.PagedResponse;
import com.azure.storage.blob.models.ListBlobsOptions;

@Singleton
@Slf4j
public class AZUtil {
  private static final String AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";
  private static final String AZURE_STORAGE_BACKUP_LOCATION = "BACKUP_LOCATION";
  private static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;

  private static String[] getSplitLocationValue(String backupLocation, Boolean isConfigLocation) {
    backupLocation = backupLocation.substring(8);
    Integer splitValue = isConfigLocation ? 2 : 3;
    String[] split = backupLocation.split("/", splitValue);
    return split;
  }

  public static void deleteKeyIfExists(JsonNode credentials, String backupLocation)
      throws Exception {
    String[] splitLocation = getSplitLocationValue(backupLocation, false);
    String azureUrl = "https://" + splitLocation[0];
    String container = splitLocation[1];
    String blob = splitLocation[2];
    String keyLocation = blob.substring(0, blob.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    String sasToken = credentials.get(AZURE_STORAGE_SAS_TOKEN_FIELDNAME).asText();
    try {
      BlobContainerClient blobContainerClient =
          createBlobContainerClient(azureUrl, sasToken, container);
      ListBlobsOptions blobsOptions = new ListBlobsOptions().setPrefix(keyLocation);
      PagedIterable<BlobItem> pagedIterable =
          blobContainerClient.listBlobs(blobsOptions, Duration.ofHours(4));
      Iterator<PagedResponse<BlobItem>> pagedResponse = pagedIterable.iterableByPage().iterator();
      if (pagedResponse.hasNext()) {
        retrieveAndDeleteObjects(pagedResponse, blobContainerClient);
      } else {
        log.info("Specified Location " + keyLocation + " does not contain objects");
        return;
      }
    } catch (Exception e) {
      log.error("Error while deleting key object from container " + container, e);
      throw e;
    }
  }

  public static boolean canCredentialListObjects(JsonNode credentials) {
    String configLocation = credentials.get(AZURE_STORAGE_BACKUP_LOCATION).asText();
    String[] splitLocation = getSplitLocationValue(configLocation, true);
    String azureUrl = "https://" + splitLocation[0];
    String container = splitLocation[1];
    String sasToken = credentials.get(AZURE_STORAGE_SAS_TOKEN_FIELDNAME).asText();
    try {
      BlobContainerClient blobContainerClient =
          createBlobContainerClient(azureUrl, sasToken, container);
      ListBlobsOptions blobsOptions = new ListBlobsOptions().setMaxResultsPerPage(1);
      PagedIterable<BlobItem> pagedIterable =
          blobContainerClient.listBlobs(blobsOptions, Duration.ofMinutes(5));
    } catch (Exception e) {
      log.error("Cannot list objects with credentials", e);
      return false;
    }
    return true;
  }

  private static BlobContainerClient createBlobContainerClient(
      String azureUrl, String sasToken, String container) {
    BlobContainerClient blobContainerClient =
        new BlobContainerClientBuilder()
            .endpoint(azureUrl)
            .sasToken(sasToken)
            .containerName(container)
            .buildClient();
    return blobContainerClient;
  }

  public static void deleteStorage(JsonNode credentials, List<String> backupLocations)
      throws Exception {
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation, false);
        String azureUrl = "https://" + splitLocation[0];
        String container = splitLocation[1];
        String blob = splitLocation[2];
        String sasToken = credentials.get(AZURE_STORAGE_SAS_TOKEN_FIELDNAME).asText();
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        ListBlobsOptions blobsOptions = new ListBlobsOptions().setPrefix(blob);
        PagedIterable<BlobItem> pagedIterable =
            blobContainerClient.listBlobs(blobsOptions, Duration.ofHours(4));
        Iterator<PagedResponse<BlobItem>> pagedResponse = pagedIterable.iterableByPage().iterator();
        log.debug("Retrieved blobs info for container " + container + " with prefix " + blob);
        retrieveAndDeleteObjects(pagedResponse, blobContainerClient);
      } catch (Exception e) {
        log.error(" Error in deleting objects at location " + backupLocation, e);
        throw e;
      }
    }
  }

  public static void retrieveAndDeleteObjects(
      Iterator<PagedResponse<BlobItem>> pagedResponse, BlobContainerClient blobContainerClient)
      throws Exception {
    while (pagedResponse.hasNext()) {
      PagedResponse<BlobItem> response = pagedResponse.next();
      BlobClient blobClient;
      for (BlobItem blobItem : response.getValue()) {
        if (blobItem.getSnapshot() != null) {
          blobClient =
              blobContainerClient.getBlobClient(blobItem.getName(), blobItem.getSnapshot());
        } else {
          blobClient = blobContainerClient.getBlobClient(blobItem.getName());
        }
        if (blobClient.exists()) {
          blobClient.delete();
        }
      }
    }
  }
}
