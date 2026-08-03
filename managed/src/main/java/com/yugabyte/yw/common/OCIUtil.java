// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.oracle.bmc.Region;
import com.oracle.bmc.auth.InstancePrincipalsAuthenticationDetailsProvider;
import com.oracle.bmc.objectstorage.ObjectStorage;
import com.oracle.bmc.objectstorage.ObjectStorageClient;
import com.oracle.bmc.objectstorage.model.BatchDeleteObjectIdentifier;
import com.oracle.bmc.objectstorage.model.BatchDeleteObjectsDetails;
import com.oracle.bmc.objectstorage.model.BatchDeleteObjectsResult;
import com.oracle.bmc.objectstorage.requests.BatchDeleteObjectsRequest;
import com.oracle.bmc.objectstorage.requests.GetObjectRequest;
import com.oracle.bmc.objectstorage.requests.ListObjectsRequest;
import com.oracle.bmc.objectstorage.responses.BatchDeleteObjectsResponse;
import com.oracle.bmc.objectstorage.responses.GetObjectResponse;
import com.oracle.bmc.objectstorage.responses.ListObjectsResponse;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData.RegionLocations;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.ProxySpec;

@Singleton
@Slf4j
public class OCIUtil implements CloudUtil {

  public static final String OCI_S3_ACCESS_KEY_ID_FIELDNAME = "OCI_S3_ACCESS_KEY_ID";

  public static final String OCI_S3_SECRET_ACCESS_KEY_FIELDNAME = "OCI_S3_SECRET_ACCESS_KEY";

  public static final String OCI_S3_HOST_BASE_FIELDNAME = "OCI_S3_HOST_BASE";

  public static final String YBC_USE_OCI_IAM_FIELDNAME = "USE_OCI_IAM";

  public static final String YBC_OCI_REGION_FIELDNAME = "OCI_REGION";

  public static final String YBC_OCI_NAMESPACE_FIELDNAME = "OCI_NAMESPACE";

  public static final String YBC_AWS_ACCESS_KEY_ID_FIELDNAME = "AWS_ACCESS_KEY_ID";

  public static final String YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME = "AWS_SECRET_ACCESS_KEY";

  public static final String YBC_AWS_ENDPOINT_FIELDNAME = "AWS_ENDPOINT";

  public static final String YBC_AWS_DEFAULT_REGION_FIELDNAME = "AWS_DEFAULT_REGION";

  /** OCI S3-compatible API always uses path-style access (no virtual-host style). */
  public static final String YBC_PATH_STYLE_ACCESS_FIELDNAME = "PATH_STYLE_ACCESS";

  public static final boolean OCI_S3_COMPAT_USE_PATH_STYLE_ACCESS = true;

  public static final boolean OCI_S3_COMPAT_USE_CHUNKED_ENCODING = false;

  public static final String OCI_NATIVE_LOCATION_PREFIX = "https://";

  public static final String OCI_NATIVE_HOST_FORMAT = "objectstorage.%s.oraclecloud.com";

  // OCI ListObjects max page size (OCI's limit).
  private static final int LIST_OBJECTS_PAGE_SIZE = 1000;

  /**
   * Native Object Storage path pattern: optional PAR /p/<token>/, then /n/<namespace>/b/<bucket>
   * then optional trailing cloud path (config prefix and/or YBA-joined backup dirs)
   */
  private static final Pattern OCI_NATIVE_PATH_PATTERN =
      Pattern.compile("^(?:/p/[^/]+)?/n/([^/]+)/b/([^/]+)(?:/(.*))?$");

  @Inject private AWSUtil awsUtil;

  public static class CloudLocationInfoOci extends CloudLocationInfo {
    public String namespace;
    public String objectStorageHost;

    public CloudLocationInfoOci(
        String objectStorageHost, String namespace, String bucket, String cloudPath) {
      super(bucket, cloudPath);
      this.namespace = namespace;
      this.objectStorageHost = objectStorageHost;
    }
  }

  /** Parsed components of a native HTTPS Object Storage URL. */
  public static class OciNativeUrlParts {
    public String host;
    public String namespace;
    public String bucket;
    public String cloudPath = "";
  }

  @Override
  public boolean isIamEnabled(CustomerConfig config) {
    return ((CustomerConfigStorageOCIData) config.getDataObject()).useOciIam;
  }

  private static boolean isS3Compatible(CustomerConfigData configData) {
    return !((CustomerConfigStorageOCIData) configData).useOciIam;
  }

  /**
   * Parse a native OCI Object Storage HTTPS URL into host / namespace / bucket / optional cloud
   * path.
   *
   * <p>Accepts any Object Storage host and an optional PAR segment /p/<token>/.
   */
  public static OciNativeUrlParts parseNativeOciUrl(String backupLocation) {
    try {
      URI uri = new URI(backupLocation);
      if (StringUtils.isBlank(uri.getHost()) || StringUtils.isBlank(uri.getPath())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Invalid OCI native backup location (missing host or path): " + backupLocation);
      }
      Matcher matcher = OCI_NATIVE_PATH_PATTERN.matcher(uri.getPath());
      if (!matcher.matches()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Invalid OCI native backup location. Expected:"
                + " https://<object-storage-host>/[p/<token>/]n/<namespace>/b/<bucket>[/<path>]");
      }
      OciNativeUrlParts parts = new OciNativeUrlParts();
      parts.host = uri.getHost();
      parts.namespace = matcher.group(1);
      parts.bucket = matcher.group(2);
      String remainder = matcher.group(3);
      if (remainder != null) {
        // Fail fast for 'o' suffixes within OCI native URLs being passed for backup location
        if (remainder.equals("o") || remainder.startsWith("o/")) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "OCI native backup locations must not use /o/<object> paths. Use"
                  + " https://<host>/n/<namespace>/b/<bucket> (bucket only for storage config).");
        }
        parts.cloudPath = remainder;
      }
      return parts;
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid OCI native backup location: " + backupLocation);
    }
  }

  /**
   * Convert the OCI S3 compatible config object to S3 native config. Hardcode path style access to
   * true, chunked encoding to false. OCI_REGION mapped to S3 signing region (fallback region).
   *
   * <p>Also, native OCI locations are normalised (get bucket from the location), and use
   * s3://<bucket>[/<path>] as s3 config's bucket. This will ensure that AWSUtil's parsing methods
   * work for these configs too.
   */
  public static CustomerConfigStorageS3Data toS3CompatibleData(
      CustomerConfigStorageOCIData ociData) {
    if (ociData.useOciIam) {
      throw new IllegalArgumentException(
          "Cannot convert native OCI IAM storage config to S3-compatible data.");
    }
    CustomerConfigStorageS3Data s3Data = new CustomerConfigStorageS3Data();
    s3Data.backupLocation = toS3StyleLocation(ociData.backupLocation);
    s3Data.immutableStorage = ociData.immutableStorage;
    s3Data.awsAccessKeyId = ociData.ociS3AccessKeyId;
    s3Data.awsSecretAccessKey = ociData.ociS3SecretAccessKey;
    s3Data.awsHostBase = ociData.ociS3HostBase;
    s3Data.fallbackRegion = ociData.ociRegion;
    s3Data.isPathStyleAccess = OCI_S3_COMPAT_USE_PATH_STYLE_ACCESS;
    s3Data.useChunkedEncoding = OCI_S3_COMPAT_USE_CHUNKED_ENCODING;
    s3Data.globalBucketAccess = false;
    s3Data.isIAMInstanceProfile = false;
    if (CollectionUtils.isNotEmpty(ociData.regionLocations)) {
      s3Data.regionLocations = new ArrayList<>();
      for (RegionLocations regionLocation : ociData.regionLocations) {
        CustomerConfigStorageS3Data.RegionLocations s3RegionLocation =
            new CustomerConfigStorageS3Data.RegionLocations();
        s3RegionLocation.region = regionLocation.region;
        s3RegionLocation.location = toS3StyleLocation(regionLocation.location);
        s3RegionLocation.awsHostBase = regionLocation.ociS3HostBase;
        // Signing region must match the regional Object Storage endpoint (not the default
        // OCI_REGION).
        s3RegionLocation.fallbackRegion =
            StringUtils.isNotBlank(regionLocation.region)
                ? regionLocation.region
                : ociData.ociRegion;
        s3RegionLocation.globalBucketAccess = false;
        s3Data.regionLocations.add(s3RegionLocation);
      }
    }
    return s3Data;
  }

  public static String toS3StyleLocation(String backupLocation) {
    if (StringUtils.isBlank(backupLocation)) {
      return backupLocation;
    }
    if (backupLocation.startsWith(AWSUtil.AWS_S3_LOCATION_PREFIX)) {
      return backupLocation;
    }
    if (backupLocation.startsWith(OCI_NATIVE_LOCATION_PREFIX)) {
      OciNativeUrlParts parts = parseNativeOciUrl(backupLocation);
      return StringUtils.isBlank(parts.cloudPath)
          ? AWSUtil.AWS_S3_LOCATION_PREFIX + parts.bucket
          : AWSUtil.AWS_S3_LOCATION_PREFIX + parts.bucket + "/" + parts.cloudPath;
    }
    throw new PlatformServiceException(
        BAD_REQUEST, "Invalid OCI backup location. Expected s3:// or https:// Object Storage URL.");
  }

  /**
   * AWSUtil only parses 's3://' locations. Backup objects / restore preflight may pass native HTTPS
   * Object Storage URLs. So normalize before any S3-compat AWSUtil delegate.
   */
  private static Set<String> toS3StyleLocations(Set<String> locations) {
    if (CollectionUtils.isEmpty(locations)) {
      return locations;
    }
    return locations.stream().map(OCIUtil::toS3StyleLocation).collect(Collectors.toSet());
  }

  private static Map<String, String> toS3StyleRegionLocationsMap(
      Map<String, String> regionLocationsMap) {
    if (MapUtils.isEmpty(regionLocationsMap)) {
      return regionLocationsMap;
    }
    Map<String, String> normalized = new HashMap<>();
    regionLocationsMap.forEach(
        (region, location) -> normalized.put(region, toS3StyleLocation(location)));
    return normalized;
  }

  private static Map<String, List<String>> toS3StyleBackupRegionLocationsMap(
      Map<String, List<String>> backupRegionLocationsMap) {
    if (MapUtils.isEmpty(backupRegionLocationsMap)) {
      return backupRegionLocationsMap;
    }
    Map<String, List<String>> normalized = new HashMap<>();
    backupRegionLocationsMap.forEach(
        (region, locations) ->
            normalized.put(
                region,
                locations == null
                    ? null
                    : locations.stream()
                        .map(OCIUtil::toS3StyleLocation)
                        .collect(Collectors.toList())));
    return normalized;
  }

  private CustomerConfigStorageS3Data requireS3CompatibleData(CustomerConfigData configData) {
    return toS3CompatibleData((CustomerConfigStorageOCIData) configData);
  }

  /**
   * Split S3-compatible locations ({@code s3://bucket/path}) or native OCI Object Storage URLs
   * ({@code https://<host>/[p/<token>/]n/<namespace>/b/<bucket>[/<path>]}).
   *
   * <p>Returns {@code [bucket]} or {@code [bucket, cloudPath]} for YBC cloud-dir construction and
   * YBA list/delete/exists against stored backup URLs.
   */
  public static String[] getSplitLocationValue(String backupLocation) {
    if (StringUtils.isBlank(backupLocation)) {
      return new String[] {""};
    }
    if (backupLocation.startsWith(AWSUtil.AWS_S3_LOCATION_PREFIX)) {
      String location = backupLocation.substring(AWSUtil.AWS_S3_LOCATION_PREFIX.length());
      return location.split("/", 2);
    }
    if (backupLocation.startsWith(OCI_NATIVE_LOCATION_PREFIX)) {
      OciNativeUrlParts parts = parseNativeOciUrl(backupLocation);
      return StringUtils.isBlank(parts.cloudPath)
          ? new String[] {parts.bucket}
          : new String[] {parts.bucket, parts.cloudPath};
    }
    throw new PlatformServiceException(
        BAD_REQUEST, "Invalid OCI backup location. Expected s3:// or https:// Object Storage URL.");
  }

  @Override
  public CloudLocationInfo getCloudLocationInfo(
      String region, CustomerConfigData configData, @Nullable String backupLocation) {
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    Map<String, String> configRegionLocationsMap = getRegionLocationsMap(configData);
    String configLocation = configRegionLocationsMap.getOrDefault(region, ociData.backupLocation);
    String locationToSplit = StringUtils.isBlank(backupLocation) ? configLocation : backupLocation;
    String[] backupSplitLocations = getSplitLocationValue(locationToSplit);
    String[] configSplitLocations = getSplitLocationValue(configLocation);
    String bucket = configSplitLocations.length > 0 ? configSplitLocations[0] : "";
    String cloudPath = backupSplitLocations.length > 1 ? backupSplitLocations[1] : "";

    String objectStorageHost;
    String namespace;
    if (ociData.useOciIam) {
      // The OCI_NAMESPACE should always be used as the correct namespace, as YBC also uses it that
      // way. We should not extract it from backup location. Prefer host from the
      // HTTPS location when present (any OCI realm / dedicated endpoint).
      // For s3:// locations, synthesize host from the placement region (same source YBC uses),
      // falling back to top-level OCI_REGION for the default location.
      String hostRegion =
          StringUtils.isBlank(region) || YbcBackupUtil.DEFAULT_REGION_STRING.equals(region)
              ? ociData.ociRegion
              : region;
      objectStorageHost = String.format(OCI_NATIVE_HOST_FORMAT, hostRegion);
      namespace = ociData.ociNamespace;
      if (configLocation.startsWith(OCI_NATIVE_LOCATION_PREFIX)) {
        OciNativeUrlParts parts = parseNativeOciUrl(configLocation);
        objectStorageHost = parts.host;
      }
    } else {
      objectStorageHost = getS3HostBase(ociData, region);
      namespace = StringUtils.defaultString(ociData.ociNamespace);
    }
    return new CloudLocationInfoOci(objectStorageHost, namespace, bucket, cloudPath);
  }

  @Override
  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    if (CollectionUtils.isNotEmpty(ociData.regionLocations)) {
      ociData.regionLocations.forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    regionLocationsMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, ociData.backupLocation);
    return regionLocationsMap;
  }

  /** Region-specific OCI S3-compatible host base, falling back to the default host base. */
  public String getS3HostBase(CustomerConfigStorageOCIData ociData, String region) {
    if (CollectionUtils.isNotEmpty(ociData.regionLocations)
        && !YbcBackupUtil.DEFAULT_REGION_STRING.equals(region)) {
      for (RegionLocations rL : ociData.regionLocations) {
        if (region.equals(rL.region) && StringUtils.isNotBlank(rL.ociS3HostBase)) {
          return rL.ociS3HostBase;
        }
      }
    }
    return ociData.ociS3HostBase;
  }

  /**
   * Builds the YBC credentials map for OCI storage.
   *
   * <p>IAM mode passes native OCI fields ({@code USE_OCI_IAM}, {@code OCI_REGION}, {@code
   * OCI_NAMESPACE}). Non-IAM (S3-compatible) mode maps Customer Secret Keys onto the AWS S3 flow in
   * YBC, and always enables path-style access.
   */
  public Map<String, String> createCredsMapYbc(CustomerConfigStorageOCIData ociData) {
    return createCredsMapYbc(ociData, YbcBackupUtil.DEFAULT_REGION_STRING);
  }

  public Map<String, String> createCredsMapYbc(
      CustomerConfigStorageOCIData ociData, String region) {
    Map<String, String> ociCredsMap = new HashMap<>();
    // Multi-region: placement REGION for YBC; default/blank falls back to OCI_REGION.
    // Set REGION to the OCI region id (no URL host parsing — matches other CSP simplicity).
    String effectiveRegion =
        StringUtils.isBlank(region) || YbcBackupUtil.DEFAULT_REGION_STRING.equals(region)
            ? ociData.ociRegion
            : region;
    if (ociData.useOciIam) {
      ociCredsMap.put(YBC_USE_OCI_IAM_FIELDNAME, "true");
      ociCredsMap.put(YBC_OCI_REGION_FIELDNAME, effectiveRegion);
      ociCredsMap.put(YBC_OCI_NAMESPACE_FIELDNAME, ociData.ociNamespace);
    } else {
      if (StringUtils.isBlank(ociData.ociS3AccessKeyId)
          || StringUtils.isBlank(ociData.ociS3SecretAccessKey)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "OCI S3-compatible storage requires OCI_S3_ACCESS_KEY_ID and OCI_S3_SECRET_ACCESS_KEY"
                + " when USE_OCI_IAM is false.");
      }
      ociCredsMap.put(YBC_AWS_ACCESS_KEY_ID_FIELDNAME, ociData.ociS3AccessKeyId);
      ociCredsMap.put(YBC_AWS_SECRET_ACCESS_KEY_FIELDNAME, ociData.ociS3SecretAccessKey);
      ociCredsMap.put(YBC_AWS_ENDPOINT_FIELDNAME, getS3HostBase(ociData, region));
      ociCredsMap.put(YBC_AWS_DEFAULT_REGION_FIELDNAME, effectiveRegion);
      ociCredsMap.put(
          YBC_PATH_STYLE_ACCESS_FIELDNAME, String.valueOf(OCI_S3_COMPAT_USE_PATH_STYLE_ACCESS));
    }
    return ociCredsMap;
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData,
      Universe universe) {
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    CloudLocationInfo csInfo = getCloudLocationInfo(region, configData, "");
    String bucket = csInfo.bucket;
    String cloudDir =
        StringUtils.isNotBlank(csInfo.cloudPath)
            ? BackupUtil.getPathWithPrefixSuffixJoin(csInfo.cloudPath, commonDir)
            : commonDir;
    cloudDir = StringUtils.isNotBlank(cloudDir) ? BackupUtil.appendSlash(cloudDir) : "";
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      csInfo = getCloudLocationInfo(region, configData, previousBackupLocation);
      previousCloudDir =
          StringUtils.isNotBlank(csInfo.cloudPath)
              ? BackupUtil.appendSlash(csInfo.cloudPath)
              : previousCloudDir;
    }
    Map<String, String> ociCredsMap = createCredsMapYbc(ociData, region);
    // IAM uses native OCI CloudType; S3-compatible static keys reuse the existing S3 path in YBC.
    String ybcCloudType = ociData.useOciIam ? Util.OCI : Util.S3;
    return YbcBackupUtil.buildCloudStoreSpec(
        bucket, cloudDir, previousCloudDir, ociCredsMap, ybcCloudType);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region,
      String cloudDir,
      CustomerConfigData configData,
      boolean isDsm,
      Universe universe) {
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    CloudLocationInfo csInfo = getCloudLocationInfo(region, configData, "");
    String bucket = csInfo.bucket;
    Map<String, String> ociCredsMap = createCredsMapYbc(ociData, region);
    String ybcCloudType = ociData.useOciIam ? Util.OCI : Util.S3;
    if (isDsm) {
      String location = getCloudLocationInfo(region, configData, cloudDir).cloudPath;
      return YbcBackupUtil.buildCloudStoreSpec(
          bucket, BackupUtil.appendSlash(location), "", ociCredsMap, ybcCloudType);
    }
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, "", ociCredsMap, ybcCloudType);
  }

  @Override
  public void checkConfigTypeAndBackupLocationSame(String backupLocation) {
    if (!(backupLocation.startsWith(AWSUtil.AWS_S3_LOCATION_PREFIX)
        || backupLocation.startsWith(OCI_NATIVE_LOCATION_PREFIX))) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          "Not an OCI location (expected s3:// or https:// Object Storage URL)");
    }
  }

  /** Validates that IAM credentials can list objects at each configured location. */
  public void validateIamConfig(CustomerConfigStorageOCIData ociData) {
    Map<String, String> regionLocationsMap = getRegionLocationsMap(ociData);
    for (Map.Entry<String, String> entry : regionLocationsMap.entrySet()) {
      CloudLocationInfoOci locationInfo =
          (CloudLocationInfoOci) getCloudLocationInfo(entry.getKey(), ociData, entry.getValue());
      try (ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo)) {
        tryListObjects(client, locationInfo.namespace, locationInfo.bucket, locationInfo.cloudPath);
      } catch (PlatformServiceException e) {
        throw e;
      } catch (Exception e) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "OCI IAM credentials cannot access location %s: %s",
                entry.getValue(), e.getMessage()));
      }
    }
  }

  public ObjectStorageClient createObjectStorageClient(CustomerConfigStorageOCIData ociData) {
    return createObjectStorageClient(ociData, ociData.ociRegion);
  }

  public ObjectStorageClient createObjectStorageClient(
      CustomerConfigStorageOCIData ociData, CloudLocationInfoOci locationInfo) {
    return createObjectStorageClient(ociData, locationInfo, createInstancePrincipalProvider());
  }

  ObjectStorageClient createObjectStorageClient(
      CustomerConfigStorageOCIData ociData,
      CloudLocationInfoOci locationInfo,
      InstancePrincipalsAuthenticationDetailsProvider provider) {
    String region = resolveClientRegion(ociData, locationInfo);
    ObjectStorageClient client = createObjectStorageClient(ociData, region, provider);
    // Native HTTPS URLs carry the realm (gov / dedicated). Apply that host after setRegion,
    // which otherwise maps the region id to the SDK's default public endpoint.
    // s3:// IAM synthesizes objectstorage.<region>.oraclecloud.com; leave setRegion alone
    // so gov region ids keep the SDK gov realm.
    String endpoint = objectStorageEndpointOverride(locationInfo.objectStorageHost, region);
    if (StringUtils.isNotBlank(endpoint)) {
      client.setEndpoint(endpoint);
    }
    return client;
  }

  // Null when the host is the commercial objectstorage.<region>.oraclecloud.com template
  // (native OC1 URLs and IAM s3:// shorthand). Non-OC1 hosts from native URLs are
  // returned as https URLs.
  static String objectStorageEndpointOverride(String host, String region) {
    String endpoint = toObjectStorageEndpoint(host);
    if (StringUtils.isBlank(endpoint) || StringUtils.isBlank(region)) {
      return endpoint;
    }
    String synthesized = toObjectStorageEndpoint(String.format(OCI_NATIVE_HOST_FORMAT, region));
    return endpoint.equals(synthesized) ? null : endpoint;
  }

  // HTTPS Object Storage endpoint from a host or URL. Blank if host is blank.
  static String toObjectStorageEndpoint(String host) {
    if (StringUtils.isBlank(host)) {
      return null;
    }
    String trimmed = host.trim();
    if (trimmed.startsWith("https://") || trimmed.startsWith("http://")) {
      return trimmed;
    }
    return "https://" + trimmed;
  }

  public ObjectStorageClient createObjectStorageClient(
      CustomerConfigStorageOCIData ociData, String region) {
    // Reject blank region before instance-principal IMDS (CI / non-OCI hosts).
    requireOciRegion(region);
    return createObjectStorageClient(ociData, region, createInstancePrincipalProvider());
  }

  ObjectStorageClient createObjectStorageClient(
      CustomerConfigStorageOCIData ociData,
      String region,
      InstancePrincipalsAuthenticationDetailsProvider provider) {
    requireOciRegion(region);
    ObjectStorageClient client = null;
    try {
      client = new ObjectStorageClient(provider);
      client.setRegion(Region.fromRegionId(region));
      return client;
    } catch (IllegalArgumentException e) {
      closeQuietly(client);
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Invalid OCI region '%s' is not a recognized OCI region identifier.", region));
    } catch (Exception e) {
      closeQuietly(client);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Failed to create OCI Object Storage client with instance principal credentials: "
              + e.getMessage());
    }
  }

  private static void requireOciRegion(String region) {
    if (StringUtils.isBlank(region)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "OCI region is required to create an Object Storage client.");
    }
  }

  private InstancePrincipalsAuthenticationDetailsProvider createInstancePrincipalProvider() {
    return InstancePrincipalsAuthenticationDetailsProvider.builder().build();
  }

  private void closeQuietly(ObjectStorageClient client) {
    if (client == null) {
      return;
    }
    try {
      client.close();
    } catch (Exception closeError) {
      log.warn("Error closing OCI Object Storage client", closeError);
    }
  }

  /**
   * Prefer the region embedded in {@code objectstorage.<region>.…} hosts; fall back to {@code
   * OCI_REGION}.
   *
   * <p>{@code OCI_REGION} stays top-level: per-location Object Storage region is already in the
   * native HTTPS host (S3-compat uses per-region {@code OCI_S3_HOST_BASE}). {@code
   * REGION_LOCATIONS.REGION} is the YBA placement key, not an OCI API region field. A regional
   * {@code OCI_REGION} would duplicate the URL.
   */
  static String resolveClientRegion(
      CustomerConfigStorageOCIData ociData, CloudLocationInfoOci locationInfo) {
    String fromHost = extractRegionFromObjectStorageHost(locationInfo.objectStorageHost);
    return StringUtils.isNotBlank(fromHost) ? fromHost : ociData.ociRegion;
  }

  /** Extracts {@code <region>} from hosts like {@code objectstorage.<region>.oraclecloud.com}. */
  static String extractRegionFromObjectStorageHost(String host) {
    if (StringUtils.isBlank(host)) {
      return null;
    }
    Matcher matcher = Pattern.compile("(?:^|\\.)objectstorage\\.([^.]+)\\.").matcher(host);
    return matcher.find() ? matcher.group(1) : null;
  }

  private void tryListObjects(
      ObjectStorage client, String namespace, String bucket, String prefix) {
    ListObjectsRequest.Builder requestBuilder =
        ListObjectsRequest.builder().namespaceName(namespace).bucketName(bucket).limit(1);
    if (StringUtils.isNotBlank(prefix)) {
      requestBuilder.prefix(prefix);
    }
    client.listObjects(requestBuilder.build());
  }

  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Map<String, String> regionLocationsMap) {
    if (isS3Compatible(configData)) {
      return awsUtil.canCredentialListObjects(
          requireS3CompatibleData(configData), toS3StyleRegionLocationsMap(regionLocationsMap));
    }
    if (MapUtils.isEmpty(regionLocationsMap)) {
      return true;
    }
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    for (Map.Entry<String, String> entry : regionLocationsMap.entrySet()) {
      try {
        CloudLocationInfoOci locationInfo =
            (CloudLocationInfoOci)
                getCloudLocationInfo(entry.getKey(), configData, entry.getValue());
        try (ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo)) {
          tryListObjects(
              client, locationInfo.namespace, locationInfo.bucket, locationInfo.cloudPath);
        }
      } catch (Exception e) {
        log.error(
            "OCI IAM credential cannot list objects in the specified backup location {}",
            entry.getValue(),
            e);
        return false;
      }
    }
    return true;
  }

  @Override
  public void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {
    if (isS3Compatible(configData)) {
      awsUtil.checkListObjectsWithYbcSuccessMarkerCloudStore(
          requireS3CompatibleData(configData), csSpec);
      return;
    }
    Map<String, ResponseCloudStoreSpec.BucketLocation> regionPrefixesMap =
        csSpec.getBucketLocationsMap();
    Map<String, String> configRegions = getRegionLocationsMap(configData);
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    for (Map.Entry<String, ResponseCloudStoreSpec.BucketLocation> regionPrefix :
        regionPrefixesMap.entrySet()) {
      String region = regionPrefix.getKey();
      if (!configRegions.containsKey(region)) {
        continue;
      }
      // Use "cloudDir" of success marker as object prefix; config bucket/namespace for listing.
      String prefix = regionPrefix.getValue().cloudDir;
      CloudLocationInfoOci locationInfo =
          (CloudLocationInfoOci) getCloudLocationInfo(region, configData, null);
      log.debug(
          "Trying object listing with OCI bucket {} namespace {} and prefix {}",
          locationInfo.bucket,
          locationInfo.namespace,
          prefix);
      try (ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo)) {
        tryListObjects(client, locationInfo.namespace, locationInfo.bucket, prefix);
      } catch (PlatformServiceException e) {
        throw e;
      } catch (Exception e) {
        String msg =
            String.format(
                "Cannot list objects in cloud location with bucket %s and cloud directory %s",
                locationInfo.bucket, prefix);
        log.error(msg, e);
        throw new PlatformServiceException(
            PRECONDITION_FAILED, msg + ": " + e.getLocalizedMessage());
      }
    }
  }

  @Override
  public void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    if (isS3Compatible(configData)) {
      awsUtil.validate(requireS3CompatibleData(configData), permissions);
      return;
    }
    validateIamConfig((CustomerConfigStorageOCIData) configData);
  }

  @Override
  public List<String> listBuckets(CustomerConfigData configData) {
    if (isS3Compatible(configData)) {
      return new ArrayList<>(awsUtil.listBuckets(requireS3CompatibleData(configData)).keySet());
    }
    // For S3 compatible, we can use AWSUtil methods. Keeping OCI IAM listBuckets method as a stub,
    // to be done as parity for Platform backups in future.
    return new ArrayList<>();
  }

  @Override
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    if (isS3Compatible(configData)) {
      return awsUtil.deleteKeyIfExists(
          requireS3CompatibleData(configData), toS3StyleLocation(defaultBackupLocation));
    }
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    CloudLocationInfoOci locationInfo =
        (CloudLocationInfoOci)
            getCloudLocationInfo(
                YbcBackupUtil.DEFAULT_REGION_STRING, configData, defaultBackupLocation);
    String objectPrefix = locationInfo.cloudPath;
    if (StringUtils.isBlank(objectPrefix) || !objectPrefix.contains("/")) {
      log.info("No key location found under {}", defaultBackupLocation);
      return true;
    }
    String keyLocation =
        objectPrefix.substring(0, objectPrefix.lastIndexOf('/')) + KEY_LOCATION_SUFFIX;
    try (ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo)) {
      deleteObjectsWithPrefix(client, locationInfo.namespace, locationInfo.bucket, keyLocation);
    } catch (Exception e) {
      log.error("Error while deleting key object at location: {}", keyLocation, e);
      return false;
    }
    return true;
  }

  @Override
  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap) {
    if (isS3Compatible(configData)) {
      return awsUtil.deleteStorage(
          requireS3CompatibleData(configData),
          toS3StyleBackupRegionLocationsMap(backupRegionLocationsMap));
    }
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    for (Map.Entry<String, List<String>> backupRegionLocations :
        backupRegionLocationsMap.entrySet()) {
      String region = backupRegionLocations.getKey();
      for (String backupLocation : backupRegionLocations.getValue()) {
        CloudLocationInfoOci locationInfo =
            (CloudLocationInfoOci) getCloudLocationInfo(region, configData, backupLocation);
        try (ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo)) {
          deleteObjectsWithPrefix(
              client, locationInfo.namespace, locationInfo.bucket, locationInfo.cloudPath);
        } catch (Exception e) {
          log.error(
              "Error occurred while deleting objects at location {}. Error {}",
              backupLocation,
              e.getMessage());
          return false;
        }
      }
    }
    return true;
  }

  private void deleteObjectsWithPrefix(
      ObjectStorage client, String namespace, String bucket, String prefix) {
    if (StringUtils.isBlank(prefix)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Refusing to delete OCI objects with an empty prefix (would wipe the bucket).");
    }
    String nextStart = null;
    do {
      ListObjectsRequest.Builder requestBuilder =
          ListObjectsRequest.builder()
              .namespaceName(namespace)
              .bucketName(bucket)
              .prefix(prefix)
              .limit(LIST_OBJECTS_PAGE_SIZE);
      if (StringUtils.isNotBlank(nextStart)) {
        requestBuilder.start(nextStart);
      }
      ListObjectsResponse response = client.listObjects(requestBuilder.build());
      if (CollectionUtils.isEmpty(response.getListObjects().getObjects())) {
        break;
      }
      // BatchDeleteObjects allows at most 1000 names per request (same as ListObjects page size).
      List<BatchDeleteObjectIdentifier> objectsToDelete =
          response.getListObjects().getObjects().stream()
              .map(
                  objectSummary ->
                      BatchDeleteObjectIdentifier.builder()
                          .objectName(objectSummary.getName())
                          .build())
              .collect(Collectors.toList());
      BatchDeleteObjectsResponse deleteResponse =
          client.batchDeleteObjects(
              BatchDeleteObjectsRequest.builder()
                  .namespaceName(namespace)
                  .bucketName(bucket)
                  .batchDeleteObjectsDetails(
                      BatchDeleteObjectsDetails.builder()
                          .objects(objectsToDelete)
                          .isSkipDeletedResult(true)
                          .build())
                  .build());
      BatchDeleteObjectsResult deleteResult = deleteResponse.getBatchDeleteObjectsResult();
      if (deleteResult != null && CollectionUtils.isNotEmpty(deleteResult.getFailed())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Failed to batch-delete %d OCI object(s) under prefix %s",
                deleteResult.getFailed().size(), prefix));
      }
      nextStart = response.getListObjects().getNextStartWith();
    } while (StringUtils.isNotBlank(nextStart));
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    if (isS3Compatible(configData)) {
      return awsUtil.getCloudFileInputStream(
          requireS3CompatibleData(configData), toS3StyleLocation(cloudPath));
    }
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    CloudLocationInfoOci locationInfo =
        (CloudLocationInfoOci)
            getCloudLocationInfo(YbcBackupUtil.DEFAULT_REGION_STRING, configData, cloudPath);
    ObjectStorageClient client = createObjectStorageClient(ociData, locationInfo);
    try {
      GetObjectResponse response =
          client.getObject(
              GetObjectRequest.builder()
                  .namespaceName(locationInfo.namespace)
                  .bucketName(locationInfo.bucket)
                  .objectName(locationInfo.cloudPath)
                  .build());
      InputStream objectStream = response.getInputStream();
      if (objectStream == null) {
        client.close();
        throw new PlatformServiceException(
            BAD_REQUEST, "No object was found at the specified location: " + cloudPath);
      }
      final ObjectStorageClient finalClient = client;
      return new FilterInputStream(objectStream) {
        private volatile boolean closed = false;

        @Override
        public void close() throws IOException {
          if (closed) {
            return;
          }
          closed = true;
          try {
            super.close();
          } finally {
            try {
              finalClient.close();
            } catch (Exception e) {
              log.warn("Error closing OCI Object Storage client", e);
            }
          }
        }
      };
    } catch (Exception e) {
      try {
        client.close();
      } catch (Exception closeError) {
        log.warn("Error closing OCI Object Storage client", closeError);
      }
      if (e instanceof PlatformServiceException) {
        throw e;
      }
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed to get OCI cloud file input stream: " + e.getMessage());
    }
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    if (isS3Compatible(configData)) {
      // Restore preflight passes Backup.storageLocation (often native HTTPS). AWSUtil needs s3://.
      return awsUtil.checkFileExists(
          requireS3CompatibleData(configData),
          toS3StyleLocations(locations),
          fileName,
          checkExistsOnAll);
    }
    CustomerConfigStorageOCIData ociData = (CustomerConfigStorageOCIData) configData;
    if (CollectionUtils.isEmpty(locations)) {
      return false;
    }
    // One instance-principal provider for the whole check: each builder.build() hits IMDS.
    InstancePrincipalsAuthenticationDetailsProvider provider = createInstancePrincipalProvider();
    try {
      AtomicInteger count = new AtomicInteger(0);
      return locations.stream()
          .map(
              location -> {
                CloudLocationInfoOci locationInfo =
                    (CloudLocationInfoOci)
                        getCloudLocationInfo(
                            YbcBackupUtil.DEFAULT_REGION_STRING, configData, location);
                String objectSuffix =
                    StringUtils.isNotBlank(locationInfo.cloudPath)
                        ? BackupUtil.getPathWithPrefixSuffixJoin(locationInfo.cloudPath, fileName)
                        : fileName;
                try (ObjectStorageClient client =
                    createObjectStorageClient(ociData, locationInfo, provider)) {
                  ListObjectsResponse response =
                      client.listObjects(
                          ListObjectsRequest.builder()
                              .namespaceName(locationInfo.namespace)
                              .bucketName(locationInfo.bucket)
                              .prefix(objectSuffix)
                              .limit(1)
                              .build());
                  if (CollectionUtils.isNotEmpty(response.getListObjects().getObjects())) {
                    count.incrementAndGet();
                  }
                } catch (Exception e) {
                  throw new RuntimeException("Error checking files on OCI locations", e);
                }
                return count;
              })
          .anyMatch(i -> checkExistsOnAll ? (i.get() == locations.size()) : (i.get() == 1));
    } catch (RuntimeException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException("Error checking files on OCI locations", e);
    }
  }

  @Override
  public boolean uploadYbaBackup(CustomerConfigData configData, File backup, String backupDir) {
    if (isS3Compatible(configData)) {
      return awsUtil.uploadYbaBackup(requireS3CompatibleData(configData), backup, backupDir);
    }
    throw new UnsupportedOperationException(
        "YBA backup upload is not supported for OCI IAM storage configs.");
  }

  @Override
  public String getYbaBackupStorageLocation(CustomerConfigData configData, String backupDir) {
    if (isS3Compatible(configData)) {
      return awsUtil.getYbaBackupStorageLocation(requireS3CompatibleData(configData), backupDir);
    }
    throw new UnsupportedOperationException(
        "YBA backup storage location is not supported for OCI IAM storage configs.");
  }

  @Override
  public List<String> getYbaBackupDirs(CustomerConfigData configData) {
    if (isS3Compatible(configData)) {
      return awsUtil.getYbaBackupDirs(requireS3CompatibleData(configData));
    }
    return new ArrayList<>();
  }

  @Override
  public File downloadYbaBackup(CustomerConfigData configData, String backupDir, Path localDir) {
    if (isS3Compatible(configData)) {
      return awsUtil.downloadYbaBackup(requireS3CompatibleData(configData), backupDir, localDir);
    }
    throw new UnsupportedOperationException(
        "YBA backup download is not supported for OCI IAM storage configs.");
  }

  @Override
  public boolean cleanupUploadedBackups(CustomerConfigData configData, String backupDir) {
    if (isS3Compatible(configData)) {
      return awsUtil.cleanupUploadedBackups(requireS3CompatibleData(configData), backupDir);
    }
    return false;
  }

  @Override
  public ProxySpec getOldProxySpec(CustomerConfigData configData) {
    if (isS3Compatible(configData)) {
      return awsUtil.getOldProxySpec(requireS3CompatibleData(configData));
    }
    return null;
  }

  @Override
  public boolean shouldUseHttpsProxy(CustomerConfigData configData) {
    if (isS3Compatible(configData)) {
      return awsUtil.shouldUseHttpsProxy(requireS3CompatibleData(configData));
    }
    return true;
  }
}
