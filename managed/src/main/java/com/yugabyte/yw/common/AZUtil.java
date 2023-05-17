// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.apache.commons.lang3.exception.ExceptionUtils.getStackTrace;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.EXPECTATION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.azure.core.http.rest.PagedIterable;
import com.azure.core.http.rest.PagedResponse;
import com.azure.core.util.BinaryData;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobStorageException;
import com.azure.storage.blob.models.ListBlobsOptions;
import com.azure.storage.blob.specialized.BlobInputStream;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;
import play.libs.Json;

@Singleton
@Slf4j
public class AZUtil implements CloudUtil {

  public static final String AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_SAS_TOKEN_FIELDNAME = "AZURE_STORAGE_SAS_TOKEN";

  public static final String YBC_AZURE_STORAGE_END_POINT_FIELDNAME = "AZURE_STORAGE_END_POINT";

  private static final String PRICING_JSON_URL =
      "https://prices.azure.com/api/retail/prices?$filter=";

  private static final String PRICE_QUERY =
      "armRegionName eq '%s' and armSkuName eq '%s' and "
          + "endsWith(productName, 'Series') and "
          + "priceType eq 'Consumption' and contains(meterName, 'Spot')";

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
        blobContainerClient.listBlobs(blobsOptions, Duration.ofMinutes(5));
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

  public BlobContainerClient createBlobContainerClient(String sasToken, String location)
      throws BlobStorageException {
    String[] splitLocation = getSplitLocationValue(location);
    String azureUrl = "https://" + splitLocation[0];
    String container = splitLocation.length > 1 ? splitLocation[1] : "";
    return createBlobContainerClient(azureUrl, sasToken, container);
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
    if (!azureSasToken.startsWith("?")) {
      azureSasToken = "?" + azureSasToken;
    }
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

  /**
   * Validates create and delete permissions on the azure configuration on default region and other
   * regions, apart from other permissions if specified.
   */
  @Override
  public void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    if (!StringUtils.isEmpty(azData.azureSasToken)) {
      validateTokenAndLocation(azData.azureSasToken, azData.backupLocation, permissions);
      if (CollectionUtils.isNotEmpty(azData.regionLocations)) {
        azData.regionLocations.stream()
            .forEach(
                location -> {
                  validateTokenAndLocation(location.azureSasToken, location.location, permissions);
                });
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Not carrying out Azure Storage Config validation because sas token is empty!");
    }
  }

  /**
   * Validates create permission on a BlobContainerClient, apart from read, list or delete
   * permissions if specified.
   */
  public void validateOnBlobContainerClient(
      BlobContainerClient blobContainerClient, List<ExtraPermissionToValidate> permissions) {
    Optional<ExtraPermissionToValidate> unsupportedPermission =
        permissions.stream()
            .filter(
                permission ->
                    permission != ExtraPermissionToValidate.READ
                        && permission != ExtraPermissionToValidate.LIST
                        && permission != ExtraPermissionToValidate.DELETE)
            .findAny();

    if (unsupportedPermission.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Unsupported permission "
              + unsupportedPermission.get().toString()
              + " validation is not supported!");
    }

    String fileName = getRandomUUID().toString() + ".txt";
    createDummyBlob(blobContainerClient, DUMMY_DATA, fileName);

    if (permissions.contains(ExtraPermissionToValidate.READ)) {
      validateReadBlob(fileName, DUMMY_DATA, blobContainerClient);
    }

    if (permissions.contains(ExtraPermissionToValidate.LIST)) {
      validateListBlobs(blobContainerClient, fileName);
    }

    if (permissions.contains(ExtraPermissionToValidate.DELETE)) {
      validateDelete(blobContainerClient, fileName);
    }
  }

  /**
   * Validates create permissions on an azure sas token and location, apart from read, list or
   * delete permissions if specified.
   */
  public void validateTokenAndLocation(
      String sasToken, String location, List<ExtraPermissionToValidate> permissions) {
    BlobContainerClient blobContainerClient = createBlobContainerClient(sasToken, location);
    validateOnBlobContainerClient(blobContainerClient, permissions);
  }

  /**
   * Deletes the given fileName from the container. Checks absence of deleted blob via list
   * operation in case list operation validation has already been done successfully. Throws
   * exception in case anything fails, hence validating.
   */
  private void validateDelete(BlobContainerClient blobContainerClient, String fileName) {
    blobContainerClient.getBlobClient(fileName).delete();
    if (containsBlobWithName(blobContainerClient, fileName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Deleted blob \"" + fileName + "\" is still in the container");
    }
  }

  /** Checks if the given fileName blob's existence can be verified via the list operation. */
  private void validateListBlobs(BlobContainerClient blobContainerClient, String fileName) {
    if (!containsBlobWithName(blobContainerClient, fileName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Created blob with name \"" + fileName + "\" not found in list.");
    }
  }

  private boolean containsBlobWithName(BlobContainerClient blobContainerClient, String fileName) {
    BlobClient blobClient = blobContainerClient.getBlobClient(fileName);
    return blobClient.exists();
  }

  private String createDummyBlob(
      BlobContainerClient blobContainerClient, String content, String dummyFileName) {
    BlobClient blobClient = blobContainerClient.getBlobClient(dummyFileName);
    blobClient.upload(BinaryData.fromString(content));
    return dummyFileName;
  }

  private void validateReadBlob(
      String fileName, String content, BlobContainerClient blobContainerClient) {
    String readString = readBlob(blobContainerClient, fileName, content.getBytes().length);
    if (!readString.equals(content)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Error reading test blob "
              + fileName
              + ", expected: \""
              + content
              + "\", got: \""
              + readString
              + "\"");
    }
  }

  private String readBlob(
      BlobContainerClient blobContainerClient, String fileName, int bytesToRead) {
    BlobClient blobClient = blobContainerClient.getBlobClient(fileName);
    BlobInputStream blobIS = blobClient.openInputStream();
    byte[] data = new byte[bytesToRead];
    try {
      blobIS.read(data);
    } catch (IOException e) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Error reading test blob " + fileName + ", exception occurred: " + getStackTrace(e));
    }
    return new String(data);
  }

  public static Double getAzuSpotPrice(String region, String instanceType) {
    try {
      String query = String.format(PRICE_QUERY, region, instanceType);
      query = URLEncoder.encode(query, StandardCharsets.UTF_8.toString());
      URL url = new URL(PRICING_JSON_URL + query);
      HttpURLConnection con = (HttpURLConnection) url.openConnection();
      con.setRequestMethod("GET");
      con.setRequestProperty("Accept-Charset", StandardCharsets.UTF_8.toString());
      con.connect();
      BufferedReader in = new BufferedReader(new InputStreamReader(con.getInputStream()));
      String inputLine;
      StringBuffer content = new StringBuffer();
      while ((inputLine = in.readLine()) != null) {
        content.append(inputLine);
      }
      in.close();
      JsonNode response = Json.mapper().readTree(content.toString());
      Double spotPrice = response.findValue("retailPrice").asDouble();
      log.info(
          "AZU spot price for instance {} in region {} is {}", instanceType, region, spotPrice);
      return spotPrice;
    } catch (Exception e) {
      log.error("Fetch Azure spot prices failed with error {}", e.getMessage());
    }
    return Double.NaN;
  }
}
