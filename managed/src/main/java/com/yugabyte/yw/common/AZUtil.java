// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.http.rest.PagedIterable;
import com.azure.core.http.rest.PagedResponse;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobStorageException;
import com.azure.storage.blob.models.ListBlobsOptions;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import java.io.InputStream;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class AZUtil implements CloudUtil {

  public static final String AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_END_POINT_FIELDNAME = "AZURE_STORAGE_END_POINT";

  public static String[] getSplitLocationValue(String backupLocation) {
    backupLocation = backupLocation.substring(8);
    String[] split = backupLocation.split("/", 3);
    return split;
  }

  @Override
  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String[] splitLocation = getSplitLocationValue(defaultBackupLocation);
    String azureUrl = "https://" + splitLocation[0];
    String container = splitLocation.length > 1 ? splitLocation[1] : "";
    String blob = splitLocation.length > 2 ? splitLocation[2] : "";
    String keyLocation = blob.substring(0, blob.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    String sasToken = azData.azureSasToken;
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
    } catch (BlobStorageException e) {
      log.error("Error while deleting key object from container " + container, e.getMessage());
      throw e;
    }
  }

  // Returns a map for <container_url, SAS token>
  private Map<String, String> getContainerTokenMap(CustomerConfigStorageAzureData azData) {
    Map<String, String> containerTokenMap = new HashMap<>();
    containerTokenMap.put(StringUtils.removeEnd(azData.backupLocation, "/"), azData.azureSasToken);
    if (CollectionUtils.isNotEmpty(azData.regionLocations)) {
      azData.regionLocations.forEach(
          (rL) -> {
            containerTokenMap.put(StringUtils.removeEnd(rL.location, "/"), rL.azureSasToken);
          });
    }
    return containerTokenMap;
  }

  @Override
  public boolean canCredentialListObjects(CustomerConfigData configData, List<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    for (String location : locations) {
      String[] splitLocation = getSplitLocationValue(location);
      String azureUrl = "https://" + splitLocation[0];
      String container = splitLocation.length > 1 ? splitLocation[1] : "";
      String containerEndpoint = String.format("%s/%s", azureUrl, container);
      String sasToken = containerTokenMap.get(containerEndpoint);
      if (StringUtils.isEmpty(sasToken)) {
        log.error("No SAS token for given location {}", location);
        return false;
      }
      try {
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        ListBlobsOptions blobsOptions = new ListBlobsOptions().setMaxResultsPerPage(1);
        PagedIterable<BlobItem> blobItems =
            blobContainerClient.listBlobs(blobsOptions, Duration.ofMinutes(5));
        if (blobItems == null) {
          return false;
        }
        if (blobItems.iterator().hasNext()) {
          BlobClient blobClient =
              blobContainerClient.getBlobClient(blobItems.iterator().next().getName());
          if (!blobClient.exists()) {
            return false;
          }
        }
      } catch (Exception e) {
        log.error(
            String.format(
                "Credential cannot list objects in the specified backup location %s", location),
            e);
        return false;
      }
    }
    return true;
  }

  public static BlobContainerClient createBlobContainerClient(
      String azureUrl, String sasToken, String container) throws BlobStorageException {
    BlobContainerClient blobContainerClient =
        new BlobContainerClientBuilder()
            .endpoint(azureUrl)
            .sasToken(sasToken)
            .containerName(container)
            .buildClient();
    return blobContainerClient;
  }

  @Override
  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String azureUrl = "https://" + splitLocation[0];
        String container = splitLocation[1];
        String blob = splitLocation[2];
        String containerEndpoint = String.format("%s/%s", azureUrl, container);
        String sasToken = containerTokenMap.get(containerEndpoint);
        if (StringUtils.isEmpty(sasToken)) {
          throw new Exception(String.format("No SAS token for given location %s", backupLocation));
        }
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        ListBlobsOptions blobsOptions = new ListBlobsOptions().setPrefix(blob);
        PagedIterable<BlobItem> pagedIterable =
            blobContainerClient.listBlobs(blobsOptions, Duration.ofHours(4));
        Iterator<PagedResponse<BlobItem>> pagedResponse = pagedIterable.iterableByPage().iterator();
        log.debug("Retrieved blobs info for container " + container + " with prefix " + blob);
        retrieveAndDeleteObjects(pagedResponse, blobContainerClient);
      } catch (BlobStorageException e) {
        log.error(" Error in deleting objects at location " + backupLocation, e.getMessage());
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

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String storageLocation,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    Pair<String, Map<String, String>> pair = getContainerCredsMapPair(configData, storageLocation);
    String cloudDir = BackupUtil.appendSlash(commonDir);
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      String[] splitValues = getSplitLocationValue(previousBackupLocation);
      previousCloudDir =
          splitValues.length > 2 ? BackupUtil.appendSlash(splitValues[2]) : previousCloudDir;
    }
    return YbcBackupUtil.buildCloudStoreSpec(
        pair.getFirst(), cloudDir, previousCloudDir, pair.getSecond(), Util.AZ);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String storageLocation, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    Pair<String, Map<String, String>> pair = getContainerCredsMapPair(configData, storageLocation);
    if (isDsm) {
      String[] splitValues = getSplitLocationValue(storageLocation);
      String location = BackupUtil.appendSlash(splitValues[2]);
      return YbcBackupUtil.buildCloudStoreSpec(
          pair.getFirst(), location, "", pair.getSecond(), Util.AZ);
    }
    return YbcBackupUtil.buildCloudStoreSpec(
        pair.getFirst(), cloudDir, "", pair.getSecond(), Util.AZ);
  }

  private Pair<String, Map<String, String>> getContainerCredsMapPair(
      CustomerConfigData configData, String storageLocation) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String[] splitValues = getSplitLocationValue(storageLocation);
    String azureUrl = "https://" + splitValues[0];
    String container = splitValues[1];
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    String containerEndpoint = String.format("%s/%s", azureUrl, container);
    String azureSasToken = containerTokenMap.get(containerEndpoint);
    Map<String, String> azCredsMap = createCredsMapYbc(azureSasToken, azureUrl);
    return new Pair(container, azCredsMap);
  }

  private Map<String, String> createCredsMapYbc(String azureSasToken, String azureContainerUrl) {
    Map<String, String> azCredsMap = new HashMap<>();
    azCredsMap.put(YBC_AZURE_STORAGE_SAS_TOKEN_FIELDNAME, azureSasToken);
    azCredsMap.put(YBC_AZURE_STORAGE_END_POINT_FIELDNAME, azureContainerUrl);
    return azCredsMap;
  }

  public List<String> listBuckets(CustomerConfigData configData) {
    // TODO Auto-generated method stub
    return new ArrayList<>();
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    if (CollectionUtils.isNotEmpty(azData.regionLocations)) {
      azData.regionLocations.stream().forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    return regionLocationsMap;
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "This method is not implemented yet");
  }
}
