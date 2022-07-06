// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.azure.core.http.rest.PagedIterable;
import com.azure.core.http.rest.PagedResponse;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobStorageException;
import com.azure.storage.blob.models.ListBlobsOptions;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import java.io.ByteArrayOutputStream;
import java.time.Duration;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.StringJoiner;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class AZUtil implements CloudUtil {

  public static final String AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_END_POINT_FIELDNAME = "AZURE_STORAGE_END_POINT";

  public static String[] getSplitLocationValue(String backupLocation, Boolean isConfigLocation) {
    backupLocation = backupLocation.substring(8);
    Integer splitValue = isConfigLocation ? 2 : 3;
    String[] split = backupLocation.split("/", splitValue);
    return split;
  }

  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String[] splitLocation = getSplitLocationValue(defaultBackupLocation, false);
    String azureUrl = "https://" + splitLocation[0];
    String container = splitLocation[1];
    String blob = splitLocation[2];
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

  public boolean canCredentialListObjects(CustomerConfigData configData, List<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    for (String configLocation : locations) {
      String[] splitLocation = getSplitLocationValue(configLocation, true);
      String azureUrl = "https://" + splitLocation[0];
      String container = splitLocation[1];
      String sasToken = azData.azureSasToken;
      try {
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        ListBlobsOptions blobsOptions = new ListBlobsOptions().setMaxResultsPerPage(1);
        blobContainerClient.listBlobs(blobsOptions, Duration.ofMinutes(5));
      } catch (Exception e) {
        log.error(
            String.format(
                "Credential cannot list objects in the specified backup location %s",
                configLocation),
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
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation, false);
        String azureUrl = "https://" + splitLocation[0];
        String container = splitLocation[1];
        String blob = splitLocation[2];
        String sasToken = azData.azureSasToken;
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
      String backupLocation, String commonDir, CustomerConfigData configData) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String[] splitValues = getSplitLocationValue(backupLocation, true);
    String azureUrl = "https://" + splitValues[0];
    String container = splitValues[1];
    String cloudDir = commonDir + "/";
    Map<String, String> azCredsMap = createCredsMapYbc(azData.azureSasToken, azureUrl);
    return YbcBackupUtil.buildCloudStoreSpec(container, cloudDir, azCredsMap, Util.AZ);
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      CustomerConfigData configData, String bucket, String dir) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String azureUrl = "https://" + bucket;
    String container = dir.split("/")[0];
    String cloudDir = dir.split("/")[1];
    Map<String, String> azCredsMap = createCredsMapYbc(azData.azureSasToken, azureUrl);
    return YbcBackupUtil.buildCloudStoreSpec(container, cloudDir, azCredsMap, Util.AZ);
  }

  @Override
  public JsonNode readFileFromCloud(String location, CustomerConfigData configData)
      throws Exception {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    String[] splitValues = getSplitLocationValue(location, false);
    String azureUrl = "https://" + splitValues[0];
    String container = splitValues[1];
    String blob = splitValues[2];
    String sasToken = azData.azureSasToken;

    BlobContainerClient blobContainerClient =
        createBlobContainerClient(azureUrl, sasToken, container);
    BlobClient blobClient = blobContainerClient.getBlobClient(blob);
    ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
    blobClient.download(outputStream);

    ObjectMapper mapper = new ObjectMapper();
    JsonNode json = mapper.readTree(new String(outputStream.toByteArray()));
    return json;
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

  @Override
  public String createDirPath(String bucket, String dir) {
    StringJoiner joiner = new StringJoiner("/");
    joiner.add("https:/").add(bucket).add(dir);
    return joiner.toString();
  }
}
