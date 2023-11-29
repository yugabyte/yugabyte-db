// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.EXPECTATION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.gax.paging.Page;
import com.google.api.gax.retrying.RetrySettings;
import com.google.auth.Credentials;
import com.google.auth.oauth2.GoogleCredentials;
import com.google.cloud.BatchResult;
import com.google.cloud.logging.LogEntry;
import com.google.cloud.logging.Logging;
import com.google.cloud.logging.Logging.EntryListOption;
import com.google.cloud.logging.LoggingOptions;
import com.google.cloud.storage.Blob;
import com.google.cloud.storage.BlobId;
import com.google.cloud.storage.BlobInfo;
import com.google.cloud.storage.Bucket;
import com.google.cloud.storage.Storage;
import com.google.cloud.storage.Storage.BlobListOption;
import com.google.cloud.storage.Storage.BucketListOption;
import com.google.cloud.storage.StorageBatch;
import com.google.cloud.storage.StorageBatchResult;
import com.google.cloud.storage.StorageException;
import com.google.cloud.storage.StorageOptions;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.UniverseInterruptionResult.InterruptionStatus;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData.RegionLocations;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.GCPCloudInfo;
import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.channels.Channels;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import java.util.stream.StreamSupport;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;
import play.libs.Json;

@Singleton
@Slf4j
public class GCPUtil implements CloudUtil {

  public static final String GCS_CREDENTIALS_JSON_FIELDNAME = "GCS_CREDENTIALS_JSON";
  private static final String GS_PROTOCOL_PREFIX = "gs://";
  private static final String HTTPS_PROTOCOL_PREFIX = "https://storage.googleapis.com/";
  private static final String PRICING_JSON_URL =
      "https://cloudpricingcalculator.appspot.com/static/data/pricelist.json";

  public static final String YBC_GOOGLE_APPLICATION_CREDENTIALS_FIELDNAME =
      "GOOGLE_APPLICATION_CREDENTIALS";

  public static final String YBC_GOOGLE_IAM_FIELDNAME = "USE_GOOGLE_IAM";

  private static JsonNode PRICE_JSON = null;
  private static final String IMAGE_PREFIX = "CP-COMPUTEENGINE-VMIMAGE-";
  private static final int DELETE_STORAGE_BATCH_REQUEST_SIZE = 100;

  public static String[] getSplitLocationValue(String location) {
    int prefixLength =
        location.startsWith(GS_PROTOCOL_PREFIX)
            ? GS_PROTOCOL_PREFIX.length()
            : (location.startsWith(HTTPS_PROTOCOL_PREFIX) ? HTTPS_PROTOCOL_PREFIX.length() : 0);

    location = location.substring(prefixLength);
    return location.split("/", 2);
  }

  @Override
  public ConfigLocationInfo getConfigLocationInfo(String location) {
    String[] splitLocations = getSplitLocationValue(location);
    String bucket = splitLocations.length > 0 ? splitLocations[0] : "";
    String cloudPath = splitLocations.length > 1 ? splitLocations[1] : "";
    return new ConfigLocationInfo(bucket, cloudPath);
  }

  public static Storage getStorageService(CustomerConfigStorageGCSData gcsData) throws IOException {
    if (gcsData.useGcpIam) {
      return getStorageService();
    } else {
      try (InputStream is =
          new ByteArrayInputStream(gcsData.gcsCredentialsJson.getBytes(StandardCharsets.UTF_8))) {
        return getStorageService(is, null);
      }
    }
  }

  public static Storage getStorageService() {
    return StorageOptions.getDefaultInstance().getService();
  }

  public static Storage getStorageService(InputStream is, RetrySettings retrySettings)
      throws IOException {
    Credentials credentials = GoogleCredentials.fromStream(is);
    StorageOptions.Builder storageOptions = StorageOptions.newBuilder().setCredentials(credentials);

    if (retrySettings != null) {
      storageOptions.setRetrySettings(retrySettings);
    }

    return storageOptions.build().getService();
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
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    String[] splitLocation = getSplitLocationValue(defaultBackupLocation);
    String bucketName = splitLocation[0];
    String objectPrefix = splitLocation[1];
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try {
      Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
      boolean deleted = storage.delete(bucketName, keyLocation);
      if (!deleted) {
        log.info("Specified Location " + keyLocation + " does not contain objects");
      } else {
        log.debug("Retrieved blobs info for bucket " + bucketName + " with prefix " + keyLocation);
      }
    } catch (Exception e) {
      log.error("Error while deleting key object at location: {}", keyLocation, e);
      return false;
    }
    return true;
  }

  private void tryListObjects(Storage storage, String bucket, String prefix) throws Exception {
    List<Storage.BlobListOption> options =
        new ArrayList<>(
            Arrays.asList(
                Storage.BlobListOption.currentDirectory(), Storage.BlobListOption.pageSize(1)));
    if (StringUtils.isNotBlank(prefix)) {
      options.add(Storage.BlobListOption.prefix(prefix));
      storage.list(bucket, options.toArray(new Storage.BlobListOption[0]));
    } else {
      storage.list(bucket, options.toArray(new Storage.BlobListOption[0]));
    }
  }

  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Collection<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return true;
    }
    try {
      Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
      for (String configLocation : locations) {
        try {
          String[] splitLocation = getSplitLocationValue(configLocation);
          String bucketName = splitLocation.length > 0 ? splitLocation[0] : "";
          String prefix = splitLocation.length > 1 ? splitLocation[1] : "";
          tryListObjects(storage, bucketName, prefix);
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
    } catch (StorageException | IOException e) {
      log.error("Failed to create GCS client", e.getMessage());
      return false;
    }
  }

  @Override
  public void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {
    Map<String, ResponseCloudStoreSpec.BucketLocation> regionPrefixesMap =
        csSpec.getBucketLocationsMap();
    Map<String, String> configRegions = getRegionLocationsMap(configData);
    try {
      Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);
      for (Map.Entry<String, ResponseCloudStoreSpec.BucketLocation> regionPrefix :
          regionPrefixesMap.entrySet()) {
        if (configRegions.containsKey(regionPrefix.getKey())) {
          // Use "cloudDir" of success marker as object prefix
          String prefix = regionPrefix.getValue().cloudDir;
          // Use config's bucket for bucket name
          ConfigLocationInfo configLocationInfo =
              getConfigLocationInfo(configRegions.get(regionPrefix.getKey()));
          String bucketName = configLocationInfo.bucket;
          log.debug("Trying object listing with GCS bucket {} and prefix {}", bucketName, prefix);
          try {
            tryListObjects(storage, bucketName, prefix);
          } catch (Exception e) {
            String msg =
                String.format(
                    "Cannot list objects in cloud location with bucket %s and cloud directory %s",
                    bucketName, prefix);
            log.error(msg, e);
            throw new PlatformServiceException(
                PRECONDITION_FAILED, msg + ": " + e.getLocalizedMessage());
          }
        }
      }
    } catch (StorageException | IOException e) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Failed to create GCS client: " + e.getLocalizedMessage());
    }
  }

  public boolean deleteStorage(CustomerConfigData configData, List<String> backupLocations) {
    for (String backupLocation : backupLocations) {
      try {
        String[] splitLocation = getSplitLocationValue(backupLocation);
        String bucketName = splitLocation[0];
        String objectPrefix = splitLocation[1];
        Storage storage = getStorageService((CustomerConfigStorageGCSData) configData);

        Page<Blob> blobs =
            storage.list(
                bucketName,
                BlobListOption.pageSize(DELETE_STORAGE_BATCH_REQUEST_SIZE),
                BlobListOption.prefix(objectPrefix));
        log.debug("Deleting blobs at location: {}", backupLocation);
        String nextPageToken = null;
        do {
          deleteBlob(storage, blobs, backupLocation);
          nextPageToken = blobs.getNextPageToken();
          if (nextPageToken != null) {
            blobs =
                storage.list(
                    bucketName,
                    BlobListOption.pageSize(DELETE_STORAGE_BATCH_REQUEST_SIZE),
                    BlobListOption.prefix(objectPrefix),
                    BlobListOption.pageToken(nextPageToken));
          }
        } while (nextPageToken != null);
      } catch (Exception e) {
        log.error(" Error in deleting objects at location " + backupLocation, e);
        return false;
      }
    }
    return true;
  }

  private void deleteBlob(Storage storage, Page<Blob> blobs, String backupLocation)
      throws InterruptedException {
    List<Blob> blobsList =
        StreamSupport.stream(blobs.getValues().spliterator(), false).collect(Collectors.toList());
    if (blobs == null || blobsList.size() == 0) {
      return;
    }

    CountDownLatch blobDeletionWaitBarrier = new CountDownLatch(blobsList.size());
    AtomicInteger failed = new AtomicInteger();
    List<StorageBatchResult<Boolean>> results = new ArrayList<>();
    StorageBatch storageBatch = storage.batch();
    blobsList.stream()
        .forEach(
            blob -> {
              if (blob != null) {
                storageBatch
                    .delete(blob.getBlobId())
                    .notify(
                        new BatchResult.Callback<>() {
                          @Override
                          public void success(Boolean result) {
                            blobDeletionWaitBarrier.countDown();
                          }

                          @Override
                          public void error(StorageException exception) {
                            log.error(exception.getMessage());
                            failed.incrementAndGet();
                            blobDeletionWaitBarrier.countDown();
                          }
                        });
              }
            });

    storageBatch.submit();
    if (!blobDeletionWaitBarrier.await(30, TimeUnit.MINUTES)) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "Timed out waiting for objects at location %s to get deleted", backupLocation));
    } else if (failed.get() > 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Encountered failures deleting objects at location %s", backupLocation));
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
    cloudDir = StringUtils.isNotBlank(cloudDir) ? BackupUtil.appendSlash(cloudDir) : "";
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

  // In case of Restore - cloudDir is picked from success marker
  // In case of Success marker download - cloud Dir is the location provided by user in API
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
    if (StringUtils.isNotBlank(gcsData.gcsCredentialsJson)) {
      gcsCredsMap.put(YBC_GOOGLE_APPLICATION_CREDENTIALS_FIELDNAME, gcsData.gcsCredentialsJson);
    } else if (gcsData.useGcpIam) {
      gcsCredsMap.put(YBC_GOOGLE_IAM_FIELDNAME, String.valueOf(gcsData.useGcpIam));
    } else {
      throw new RuntimeException(
          "Neither 'GCS_CREDENTIALS_JSON' nor 'USE_GCP_IAM' are present in the backup config.");
    }
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

  public static Double getGcpSpotPrice(String region, String instanceType) {
    instanceType = IMAGE_PREFIX + instanceType + "-preemptible";
    instanceType = instanceType.toUpperCase();

    try {
      // Fetch prices only once
      if (PRICE_JSON == null) {
        URL url = new URL(PRICING_JSON_URL);
        HttpURLConnection con = (HttpURLConnection) url.openConnection();
        con.setRequestMethod("GET");
        con.connect();
        BufferedReader in = new BufferedReader(new InputStreamReader(con.getInputStream()));
        String inputLine;
        StringBuffer content = new StringBuffer();
        while ((inputLine = in.readLine()) != null) {
          content.append(inputLine);
        }
        in.close();
        JsonNode response = Json.mapper().readTree(content.toString());
        PRICE_JSON = response.get("gcp_price_list");
      }
      JsonNode prices = PRICE_JSON.findValue(instanceType);
      Double spotPrice = prices.findValue(region).asDouble();
      log.info(
          "GCP spot price for instance {} in region {} is {}", instanceType, region, spotPrice);
      return spotPrice;
    } catch (Exception e) {
      log.error("Fetch gcp spot prices failed with error {}", e.getMessage());
    }
    return Double.NaN;
  }

  /**
   * Validates create permission on the GCP configuration on default region and other regions, apart
   * from other permissions if specified.
   */
  @Override
  public void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    CustomerConfigStorageGCSData gcsData = (CustomerConfigStorageGCSData) configData;
    if (!StringUtils.isEmpty(gcsData.gcsCredentialsJson)) {
      Storage storage = null;
      try {
        storage = getStorageService(gcsData);
      } catch (IOException ex) {
        throw new PlatformServiceException(
            EXPECTATION_FAILED, "Error while creating Storage service from GCS Data!");
      }

      validateOnLocation(storage, gcsData.backupLocation, permissions);

      if (CollectionUtils.isNotEmpty(gcsData.regionLocations)) {
        for (RegionLocations location : gcsData.regionLocations) {
          if (StringUtils.isEmpty(location.region)) {
            throw new PlatformServiceException(
                EXPECTATION_FAILED, "Region of a location cannot be empty.");
          }

          validateOnLocation(storage, location.location, permissions);
        }
      }
    } else {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "CRUD Validation for GCP Backup Configuration not carried out"
              + " because JSON Credentials are null.");
    }
  }

  /** Validates create permission on a Bucket, apart from read or list permissions if specified. */
  public void validateOnBucket(
      Storage storage,
      String bucketName,
      String prefix,
      List<ExtraPermissionToValidate> permissions) {
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
    String completeFileName = BackupUtil.getPathWithPrefixSuffixJoin(prefix, fileName);

    createObject(storage, bucketName, DUMMY_DATA, completeFileName);

    if (permissions.contains(ExtraPermissionToValidate.READ)) {
      validateReadBlob(storage, bucketName, completeFileName, DUMMY_DATA);
    }

    if (permissions.contains(ExtraPermissionToValidate.LIST)) {
      validateListBlobs(storage, bucketName, completeFileName);
    }

    validateDelete(storage, bucketName, completeFileName);
  }

  /**
   * Validates create permission on a GCP backup location, apart from read or list permissions if
   * specified.
   */
  private void validateOnLocation(
      Storage storage, String location, List<ExtraPermissionToValidate> permissions) {
    ConfigLocationInfo locationInfo = getConfigLocationInfo(location);
    validateOnBucket(storage, locationInfo.bucket, locationInfo.cloudPath, permissions);
  }

  /**
   * Deletes the given fileName from the container. Checks absence of deleted blob via read
   * operation. Throws exception in case anything fails, hence validating.
   */
  private void validateDelete(Storage storage, String bucketName, String fileName) {
    if (!storage.delete(BlobId.of(bucketName, fileName))) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED,
          "Deletion of test blob " + fileName + " could not proceed because it was not found.");
    }
    if (containsBlobWithName(storage, bucketName, fileName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Deleted blob \"" + fileName + "\" is still in the bucket.");
    }
  }

  /** Checks if the given fileName blob's existence can be verified via the list operation. */
  private void validateListBlobs(Storage storage, String bucketName, String fileName) {
    if (!listContainsBlobWithName(storage, bucketName, fileName)) {
      throw new PlatformServiceException(
          EXPECTATION_FAILED, "Created blob with name \"" + fileName + "\" not found in list.");
    }
  }

  private boolean listContainsBlobWithName(Storage storage, String bucketName, String fileName) {
    Optional<Blob> blob =
        StreamSupport.stream(
                storage
                    .list(
                        bucketName,
                        new Storage.BlobListOption[] {Storage.BlobListOption.prefix(fileName)})
                    .iterateAll()
                    .spliterator(),
                false)
            .filter(b -> b.getName().equals(fileName))
            .findAny();
    return blob.isPresent();
  }

  private boolean containsBlobWithName(Storage storage, String bucketName, String fileName) {
    return storage.get(bucketName, fileName) != null;
  }

  private void createObject(Storage storage, String bucketName, String content, String fileName) {
    storage.create(
        BlobInfo.newBuilder(BlobId.of(bucketName, fileName)).setContentType("text/plain").build(),
        content.getBytes());
  }

  private void validateReadBlob(
      Storage storage, String bucketName, String fileName, String content) {
    String readString = readBlob(storage, bucketName, fileName, content.getBytes().length);
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

  private String readBlob(Storage storage, String bucketName, String fileName, int bytesToRead) {
    byte[] readBytes = storage.readAllBytes(bucketName, fileName);
    return new String(readBytes);
  }

  public UniverseInterruptionResult spotInstanceUniverseStatus(Universe universe) {
    UniverseInterruptionResult result = new UniverseInterruptionResult(universe.getName());

    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    Provider primaryClusterProvider =
        Provider.getOrBadRequest(UUID.fromString(userIntent.provider));
    GCPCloudInfo primaryGcpInfo = primaryClusterProvider.getDetails().getCloudInfo().getGcp();
    String startTime = universe.getCreationDate().toInstant().toString().substring(0, 10);
    UUID primaryClusterUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;

    // For nodes in primary cluster
    for (final NodeDetails nodeDetails : universe.getNodesInCluster(primaryClusterUUID)) {
      result.addNodeStatus(
          nodeDetails.nodeName,
          isSpotInstanceInterrupted(
                  nodeDetails.nodeName, nodeDetails.getZone(), startTime, primaryGcpInfo)
              ? InterruptionStatus.Interrupted
              : InterruptionStatus.NotInterrupted);
    }
    // For nodes in read replicas
    for (Cluster cluster : universe.getUniverseDetails().getReadOnlyClusters()) {
      Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
      GCPCloudInfo gcpInfo = provider.getDetails().getCloudInfo().getGcp();
      for (final NodeDetails nodeDetails : universe.getNodesInCluster(cluster.uuid)) {
        result.addNodeStatus(
            nodeDetails.nodeName,
            isSpotInstanceInterrupted(
                    nodeDetails.nodeName, nodeDetails.getZone(), startTime, gcpInfo)
                ? InterruptionStatus.Interrupted
                : InterruptionStatus.NotInterrupted);
      }
    }
    return result;
  }

  private boolean isSpotInstanceInterrupted(
      String instanceName, String zone, String startTime, GCPCloudInfo gcpInfo) {
    try {
      String project = gcpInfo.getGceProject();
      String logName =
          String.format("logName=projects/%s/logs", project)
              + "/cloudaudit.googleapis.com%2Fsystem_event";
      String logFilter =
          String.format(
              "resource.labels.zone=%s AND "
                  + "%s AND "
                  + "protoPayload.methodName=compute.instances.preempted AND timestamp>=%s "
                  + "AND protoPayload.resourceName=projects/%s/zones/%s/instances/%s",
              zone, logName, startTime, project, zone, instanceName);

      String path = gcpInfo.getGceApplicationCredentialsPath();
      GoogleCredentials creds = GoogleCredentials.fromStream(new FileInputStream(path));
      try (Logging logging =
          LoggingOptions.newBuilder()
              .setProjectId(project)
              .setCredentials(creds)
              .build()
              .getService()) {
        Page<LogEntry> entries = logging.listLogEntries(EntryListOption.filter(logFilter));
        while (entries != null) {
          for (LogEntry logEntry : entries.iterateAll()) {
            if (logEntry.getPayload().getData().toString().contains("Instance was preempted")) {
              return true;
            }
          }
          entries = entries.getNextPage();
        }
      }
      return false;
    } catch (Exception e) {
      e.printStackTrace();
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Fetch interruptions status for GCP instance failed with " + e.getMessage());
    }
  }
}
