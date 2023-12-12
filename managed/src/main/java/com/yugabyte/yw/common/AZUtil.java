// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.apache.commons.lang3.exception.ExceptionUtils.getStackTrace;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.EXPECTATION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.azure.core.http.rest.PagedIterable;
import com.azure.core.http.rest.PagedResponse;
import com.azure.core.management.AzureEnvironment;
import com.azure.core.management.profile.AzureProfile;
import com.azure.core.util.BinaryData;
import com.azure.core.util.IterableStream;
import com.azure.identity.ClientSecretCredential;
import com.azure.identity.ClientSecretCredentialBuilder;
import com.azure.resourcemanager.AzureResourceManager;
import com.azure.resourcemanager.monitor.fluent.models.EventDataInner;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.BlobContainerClientBuilder;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobStorageException;
import com.azure.storage.blob.models.ListBlobsOptions;
import com.azure.storage.blob.specialized.BlobInputStream;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.UniverseInterruptionResult.InterruptionStatus;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.AzureCloudInfo;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.StreamSupport;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
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

  public class CloudLocationInfoAzure extends CloudLocationInfo {
    public String azureUrl;

    public CloudLocationInfoAzure(String azureUrl, String bucket, String cloudPath) {
      super(bucket, cloudPath);
      this.azureUrl = azureUrl;
    }
  }

  /*
   * For Azure location like https://azurl.storage.net/testcontainer/suffix,
   * splitLocation[0] is equal to azurl.storage.net
   * splitLocation[1] is equal to testcontainer
   * splitLocation[2] is equal to the suffix part of string
   */
  public static String[] getSplitLocationValue(String backupLocation) {
    backupLocation = backupLocation.substring(8);
    String[] split = backupLocation.split("/", 3);
    return split;
  }

  @Override
  public CloudLocationInfo getCloudLocationInfo(
      String region, CustomerConfigData configData, @Nullable String backupLocation) {
    CustomerConfigStorageAzureData s3Data = (CustomerConfigStorageAzureData) configData;
    Map<String, String> configRegionLocationsMap = getRegionLocationsMap(configData);
    String configLocation = configRegionLocationsMap.getOrDefault(region, s3Data.backupLocation);
    String[] backupSplitLocations =
        getSplitLocationValue(
            StringUtils.isBlank(backupLocation) ? configLocation : backupLocation);
    String[] configSplitLocations = getSplitLocationValue(configLocation);
    String azureUrl = "https://" + configSplitLocations[0];
    String bucket = configSplitLocations.length > 1 ? configSplitLocations[1] : "";
    String cloudPath = backupSplitLocations.length > 2 ? backupSplitLocations[2] : "";
    return new CloudLocationInfoAzure(azureUrl, bucket, cloudPath);
  }

  @Override
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    CloudLocationInfoAzure cLInfo =
        (CloudLocationInfoAzure)
            getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, configData, defaultBackupLocation);
    String azureUrl = cLInfo.azureUrl;
    String container = cLInfo.bucket;
    String blob = cLInfo.cloudPath;
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
      }
    } catch (Exception e) {
      log.error("Error while deleting key object at location: {}", keyLocation, e);
      return false;
    }
    return true;
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

  private void tryListObjects(BlobContainerClient client, String prefix) throws Exception {
    ListBlobsOptions blobsOptions = new ListBlobsOptions().setMaxResultsPerPage(1);
    if (StringUtils.isNotBlank(prefix)) {
      blobsOptions.setPrefix(prefix);
    }
    PagedIterable<BlobItem> blobItems = client.listBlobs(blobsOptions, Duration.ofMinutes(5));
    if (blobItems == null) {
      throw new Exception("Fetched blobs iterable cannot be null");
    }
    blobItems.iterator().hasNext();
  }

  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Map<String, String> regionLocationsMap) {
    if (MapUtils.isEmpty(regionLocationsMap)) {
      return true;
    }
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    for (Map.Entry<String, String> entry : regionLocationsMap.entrySet()) {
      String region = entry.getKey();
      String location = entry.getValue();
      CloudLocationInfoAzure cLInfo =
          (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, location);
      String azureUrl = cLInfo.azureUrl;
      String container = cLInfo.bucket;
      String containerEndpoint = String.format("%s/%s", azureUrl, container);
      String sasToken = containerTokenMap.get(containerEndpoint);
      if (StringUtils.isEmpty(sasToken)) {
        log.error("No SAS token for given location {}", location);
        return false;
      }
      try {
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        tryListObjects(blobContainerClient, null);
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

  @Override
  public void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {
    Map<String, ResponseCloudStoreSpec.BucketLocation> regionPrefixesMap =
        csSpec.getBucketLocationsMap();
    Map<String, String> configRegions = getRegionLocationsMap(configData);
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    for (Map.Entry<String, ResponseCloudStoreSpec.BucketLocation> regionPrefix :
        regionPrefixesMap.entrySet()) {
      if (configRegions.containsKey(regionPrefix.getKey())) {
        // Use "cloudDir" of success marker as object prefix
        String prefix = regionPrefix.getValue().cloudDir;
        // Use config's Azure Url and container
        CloudLocationInfoAzure cLInfo =
            (CloudLocationInfoAzure) getCloudLocationInfo(regionPrefix.getKey(), configData, null);
        String container = cLInfo.bucket;
        String azureUrl = cLInfo.azureUrl;
        String containerEndpoint = String.format("%s/%s", azureUrl, container);
        String sasToken = containerTokenMap.get(containerEndpoint);
        log.debug(
            "Trying object listing with Azure URL {} and prefix {}", containerEndpoint, prefix);
        try {
          BlobContainerClient blobContainerClient =
              createBlobContainerClient(azureUrl, sasToken, container);
          tryListObjects(blobContainerClient, prefix);
        } catch (Exception e) {
          String msg =
              String.format(
                  "Cannot list objects in cloud location with container endpoint %s and cloud"
                      + " directory %s",
                  containerEndpoint, prefix);
          log.error(msg, e);
          throw new PlatformServiceException(
              PRECONDITION_FAILED, msg + ": " + e.getLocalizedMessage());
        }
      }
    }
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
  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    for (Map.Entry<String, List<String>> backupRegionLocations :
        backupRegionLocationsMap.entrySet()) {
      String region = backupRegionLocations.getKey();
      try {
        CloudLocationInfoAzure cLInfo =
            (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, "");
        String azureUrl = cLInfo.azureUrl;
        String container = cLInfo.bucket;
        String containerEndpoint = String.format("%s/%s", azureUrl, container);
        String sasToken = containerTokenMap.get(containerEndpoint);
        if (StringUtils.isEmpty(sasToken)) {
          log.error("No SAS token for given region {}, container {}", region, container);
        }
        BlobContainerClient blobContainerClient =
            createBlobContainerClient(azureUrl, sasToken, container);
        for (String backupLocation : backupRegionLocations.getValue()) {
          try {
            cLInfo =
                (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, backupLocation);
            String blob = cLInfo.cloudPath;
            ListBlobsOptions blobsOptions = new ListBlobsOptions().setPrefix(blob);
            PagedIterable<BlobItem> pagedIterable =
                blobContainerClient.listBlobs(blobsOptions, Duration.ofHours(4));
            Iterator<PagedResponse<BlobItem>> pagedResponse =
                pagedIterable.iterableByPage().iterator();
            log.debug("Retrieved blobs info for container " + container + " with prefix " + blob);
            retrieveAndDeleteObjects(pagedResponse, blobContainerClient);
          } catch (BlobStorageException e) {
            log.error(
                "Error occured while deleting objects at location {}. Error {}",
                backupLocation,
                e.getMessage());
            return false;
          }
        }
      } catch (BlobStorageException e) {
        log.error(" Error occured while deleting objects in Azure: {}", e.getMessage());
        return false;
      }
    }
    return true;
  }

  public static void retrieveAndDeleteObjects(
      Iterator<PagedResponse<BlobItem>> pagedResponse, BlobContainerClient blobContainerClient) {
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
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    Pair<String, Map<String, String>> pair = getContainerCredsMapPair(configData, region);
    CloudLocationInfoAzure csInfoAzure =
        (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, "");
    String cloudDir =
        StringUtils.isNotBlank(csInfoAzure.cloudPath)
            ? BackupUtil.getPathWithPrefixSuffixJoin(csInfoAzure.cloudPath, commonDir)
            : commonDir;
    cloudDir = StringUtils.isNotBlank(cloudDir) ? BackupUtil.appendSlash(cloudDir) : "";
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      csInfoAzure =
          (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, previousBackupLocation);
      previousCloudDir =
          StringUtils.isNotBlank(csInfoAzure.cloudPath)
              ? BackupUtil.appendSlash(csInfoAzure.cloudPath)
              : previousCloudDir;
    }
    return YbcBackupUtil.buildCloudStoreSpec(
        pair.getFirst(), cloudDir, previousCloudDir, pair.getSecond(), Util.AZ);
  }

  // In case of Restore - cloudDir is picked from success marker
  // In case of Success marker download - cloud Dir is the location provided by user in API
  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    Pair<String, Map<String, String>> pair = getContainerCredsMapPair(configData, region);
    if (isDsm) {
      CloudLocationInfoAzure csInfoAzure =
          (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, cloudDir);
      String location = BackupUtil.appendSlash(csInfoAzure.cloudPath);
      return YbcBackupUtil.buildCloudStoreSpec(
          pair.getFirst(), location, "", pair.getSecond(), Util.AZ);
    }
    return YbcBackupUtil.buildCloudStoreSpec(
        pair.getFirst(), cloudDir, "", pair.getSecond(), Util.AZ);
  }

  private Pair<String, Map<String, String>> getContainerCredsMapPair(
      CustomerConfigData configData, String region) {
    CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
    CloudLocationInfoAzure csInfoAzure =
        (CloudLocationInfoAzure) getCloudLocationInfo(region, configData, "");
    String azureUrl = csInfoAzure.azureUrl;
    String container = csInfoAzure.bucket;
    Map<String, String> containerTokenMap = getContainerTokenMap(azData);
    String containerEndpoint = String.format("%s/%s", azureUrl, container);
    String azureSasToken = containerTokenMap.get(containerEndpoint);
    Map<String, String> azCredsMap = createCredsMapYbc(azureSasToken, azureUrl);
    return new Pair<String, Map<String, String>>(container, azCredsMap);
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
    regionLocationsMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, azData.backupLocation);
    return regionLocationsMap;
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "This method is not implemented yet");
  }

  @Override
  /*
   * For Azure location like https://azurl.storage.net/testcontainer/suffix,
   * splitLocation[0] is equal to azurl.storage.net
   * splitLocation[1] is equal to testcontainer
   * splitLocation[2] is equal to the suffix part of string
   */
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    try {
      CustomerConfigStorageAzureData azData = (CustomerConfigStorageAzureData) configData;
      BlobContainerClient blobContainerClient =
          createBlobContainerClient(azData.azureSasToken, azData.backupLocation);
      AtomicInteger count = new AtomicInteger(0);
      return locations.stream()
          .map(
              l -> {
                CloudLocationInfoAzure cLInfo =
                    (CloudLocationInfoAzure)
                        getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, configData, l);
                String blob = cLInfo.cloudPath;

                // objectSuffix is the exact suffix with file name
                String objectSuffix =
                    StringUtils.isNotBlank(blob)
                        ? BackupUtil.getPathWithPrefixSuffixJoin(blob, fileName)
                        : fileName;
                BlobClient blobClient = blobContainerClient.getBlobClient(objectSuffix);
                if (blobClient.exists()) {
                  count.incrementAndGet();
                }
                return count;
              })
          .anyMatch(i -> checkExistsOnAll ? (i.get() == locations.size()) : (i.get() == 1));
    } catch (Exception e) {
      throw new RuntimeException("Error checking files on locations", e);
    }
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
                        && permission != ExtraPermissionToValidate.LIST)
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

    validateDelete(blobContainerClient, fileName);
  }

  /**
   * Validates create permissions on an azure sas token and location, apart from read, list or
   * delete permissions if specified.
   */
  private void validateTokenAndLocation(
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
    if (!listContainsBlobWithName(blobContainerClient, fileName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Created blob with name \"" + fileName + "\" not found in list.");
    }
  }

  private boolean listContainsBlobWithName(
      BlobContainerClient blobContainerClient, String fileName) {
    Optional<BlobItem> blobItem =
        StreamSupport.stream(blobContainerClient.listBlobs().spliterator(), true)
            .filter(b -> b.getName().equals(fileName))
            .findAny();
    return blobItem.isPresent();
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

  public UniverseInterruptionResult spotInstanceUniverseStatus(Universe universe) {
    UniverseInterruptionResult result = new UniverseInterruptionResult(universe.getName());
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    Provider primaryClusterProvider =
        Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    String startTime = universe.getCreationDate().toInstant().toString();
    UUID primaryClusterUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;

    // For nodes in primary cluster
    for (final NodeDetails nodeDetails : universe.getNodesInCluster(primaryClusterUUID)) {
      result.addNodeStatus(
          nodeDetails.nodeName,
          isSpotInstanceInterrupted(nodeDetails.nodeName, primaryClusterProvider, startTime)
              ? InterruptionStatus.Interrupted
              : InterruptionStatus.NotInterrupted);
    }
    // For nodes in read replicas
    for (Cluster cluster : universe.getUniverseDetails().getReadOnlyClusters()) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      for (final NodeDetails nodeDetails : universe.getNodesInCluster(cluster.uuid)) {
        result.addNodeStatus(
            nodeDetails.nodeName,
            isSpotInstanceInterrupted(nodeDetails.nodeName, provider, startTime)
                ? InterruptionStatus.Interrupted
                : InterruptionStatus.NotInterrupted);
      }
    }
    return result;
  }

  private boolean isSpotInstanceInterrupted(String nodeName, Provider provider, String startTime) {
    try {
      AzureCloudInfo azuInfo = provider.getDetails().getCloudInfo().getAzu();
      AzureProfile profile = new AzureProfile(AzureEnvironment.AZURE);
      ClientSecretCredential clientSecretCredential =
          new ClientSecretCredentialBuilder()
              .clientId(azuInfo.getAzuClientId())
              .clientSecret(azuInfo.getAzuClientSecret())
              .tenantId(azuInfo.getAzuTenantId())
              .build();
      AzureResourceManager azure =
          AzureResourceManager.authenticate(clientSecretCredential, profile)
              .withSubscription(azuInfo.getAzuSubscriptionId());
      String resourceID =
          String.format(
              "/SUBSCRIPTIONS/%s/RESOURCEGROUPS/%s/PROVIDERS/MICROSOFT.COMPUTE/VIRTUALMACHINES/%s",
              azuInfo.azuSubscriptionId, azuInfo.azuRG, nodeName);

      String filter =
          String.format(
              "eventTimestamp ge '%s' and " + "eventTimestamp le '%s' and resourceID eq '%s'",
              startTime, Instant.now().toString(), resourceID);

      PagedIterable<EventDataInner> eventList =
          azure.diagnosticSettings().manager().serviceClient().getActivityLogs().list(filter);

      for (PagedResponse<EventDataInner> resp : eventList.iterableByPage()) {
        IterableStream<EventDataInner> events = resp.getElements();
        for (EventDataInner event : events) {
          String operationName = event.operationName().value().toLowerCase(),
              status = event.status().value();
          if (operationName.contains("evictspotvm") && status.equalsIgnoreCase("Succeeded")) {
            return true;
          }
        }
      }
      return false;
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Fetch interruptions status for AZURE failed with" + e.getMessage());
    }
  }
}
