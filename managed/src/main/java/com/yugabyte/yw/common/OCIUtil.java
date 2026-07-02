// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.backuprestore.AdvancedRestorePreflightParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData.RegionLocations;
import java.io.InputStream;
import java.net.URI;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.annotation.Nullable;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.CloudStoreSpec;

@Singleton
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

  public static final String OCI_NATIVE_LOCATION_PREFIX = "https://";

  public static final String OCI_NATIVE_HOST_FORMAT = "objectstorage.%s.oraclecloud.com";

  /**
   * Native Object Storage path pattern: optional PAR /p/<token>/, then /n/<namespace>/b/<bucket>
   *
   * <p>Group 1 = namespace, group 2 = bucket.
   */
  private static final Pattern OCI_NATIVE_PATH_PATTERN =
      Pattern.compile("^(?:/p/[^/]+)?/n/([^/]+)/b/([^/]+)/?$");

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
  }

  @Override
  public boolean isIamEnabled(CustomerConfig config) {
    return ((CustomerConfigStorageOCIData) config.getDataObject()).useOciIam;
  }

  /**
   * Parse a native OCI Object Storage HTTPS URL into host / namespace / bucket.
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
                + " https://<object-storage-host>/[p/<token>/]n/<namespace>/b/<bucket>");
      }
      OciNativeUrlParts parts = new OciNativeUrlParts();
      parts.host = uri.getHost();
      parts.namespace = matcher.group(1);
      parts.bucket = matcher.group(2);
      return parts;
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid OCI native backup location: " + backupLocation);
    }
  }

  /**
   * Split S3-compatible locations ({@code s3://bucket/path}) or native OCI Object Storage URLs
   * ({@code https://<host>/[p/<token>/]n/<namespace>/b/<bucket>}).
   *
   * <p>Returns {@code [bucket]} or {@code [bucket, cloudPath]} for YBC cloud-dir construction.
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
      return new String[] {parts.bucket};
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
      // Prefer host/namespace from the HTTPS location when present (any OCI realm / dedicated
      // endpoint). Fall back to OC1 host format from OCI_REGION + config namespace.
      objectStorageHost = String.format(OCI_NATIVE_HOST_FORMAT, ociData.ociRegion);
      namespace = ociData.ociNamespace;
      if (configLocation.startsWith(OCI_NATIVE_LOCATION_PREFIX)) {
        OciNativeUrlParts parts = parseNativeOciUrl(configLocation);
        objectStorageHost = parts.host;
        namespace = parts.namespace;
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
   * <p>IAM mode passes native OCI fields: USE_OCI_IAM, OCI_REGION, OCI_NAMESPACE). Non-IAM
   * (S3-compatible) mode maps Customer Secret Keys onto the AWS S3 flow in YBC, and always enables
   * path-style access.
   */
  public Map<String, String> createCredsMapYbc(CustomerConfigStorageOCIData ociData) {
    return createCredsMapYbc(ociData, YbcBackupUtil.DEFAULT_REGION_STRING);
  }

  public Map<String, String> createCredsMapYbc(
      CustomerConfigStorageOCIData ociData, String region) {
    Map<String, String> ociCredsMap = new HashMap<>();
    if (ociData.useOciIam) {
      ociCredsMap.put(YBC_USE_OCI_IAM_FIELDNAME, "true");
      ociCredsMap.put(YBC_OCI_REGION_FIELDNAME, ociData.ociRegion);
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
      ociCredsMap.put(YBC_AWS_DEFAULT_REGION_FIELDNAME, ociData.ociRegion);
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

  @Override
  public boolean canCredentialListObjects(
      CustomerConfigData configData, Map<String, String> locations) {
    // TODO: OCI - implement list check; return true for now so YBC backup create can proceed.
    return true;
  }

  @Override
  public void checkListObjectsWithYbcSuccessMarkerCloudStore(
      CustomerConfigData configData, YbcBackupResponse.ResponseCloudStoreSpec csSpec) {
    // TODO: OCI
  }

  @Override
  public void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    // TODO: OCI
    throw new UnsupportedOperationException("OCI validate not implemented yet");
  }

  @Override
  public List<String> listBuckets(CustomerConfigData configData) {
    // TODO: OCI
    throw new UnsupportedOperationException("OCI listBuckets not implemented yet");
  }

  @Override
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    // TODO: OCI
    throw new UnsupportedOperationException("OCI deleteKeyIfExists not implemented yet");
  }

  @Override
  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap) {
    // TODO: OCI
    throw new UnsupportedOperationException("OCI deleteStorage not implemented yet");
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    // TODO: OCI
    throw new UnsupportedOperationException("OCI getCloudFileInputStream not implemented yet");
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    // TODO: OCI - also drop generateYBBackupRestorePreflightResponse override once this exists.
    throw new UnsupportedOperationException("OCI checkFileExists not implemented yet");
  }

  @Override
  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      AdvancedRestorePreflightParams preflightParams, CustomerConfigData configData) {
    // TODO: OCI - remove after checkFileExists; use CloudUtil default like AWS/GCS/Azure.
    throw new PlatformServiceException(
        BAD_REQUEST,
        "Not implemented yet: OCI checkFileExists is required for YB_BACKUP_SCRIPT restore"
            + " preflight.");
  }
}
