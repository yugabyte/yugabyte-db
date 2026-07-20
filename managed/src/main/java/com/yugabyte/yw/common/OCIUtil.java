// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.backuprestore.AdvancedRestorePreflightParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageOCIData;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.ybc.CloudStoreSpec;

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

  @Override
  public boolean isIamEnabled(CustomerConfig config) {
    return ((CustomerConfigStorageOCIData) config.getDataObject()).useOciIam;
  }

  @Override
  public CloudLocationInfo getCloudLocationInfo(
      String region, CustomerConfigData configData, @Nullable String backupLocation) {
    // TODO: Implement native IAM and S3-compatible location parsing.
    throw notImplementedException();
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

  /** Builds the YBC credentials map for OCI storage. YBC integration is pending. */
  public Map<String, String> createCredsMapYbc(CustomerConfigStorageOCIData ociData) {
    // TODO: Implement native IAM and S3-compatible credential mapping for YBC.
    // For S3-compatible mode, always pass PATH_STYLE_ACCESS=true
    // (OCI_S3_COMPAT_USE_PATH_STYLE_ACCESS).
    throw notImplementedException();
  }

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData,
      Universe universe) {
    throw ybcNotSupportedException();
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region,
      String cloudDir,
      CustomerConfigData configData,
      boolean isDsm,
      Universe universe) {
    throw ybcNotSupportedException();
  }

  @Override
  public List<String> listBuckets(CustomerConfigData configData) {
    // TODO: Implement OCI bucket listing.
    return new ArrayList<>();
  }

  @Override
  public boolean deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation) {
    // TODO: Implement OCI object deletion.
    return false;
  }

  @Override
  public boolean deleteStorage(
      CustomerConfigData configData, Map<String, List<String>> backupRegionLocationsMap) {
    // TODO: Implement OCI storage deletion.
    return false;
  }

  @Override
  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception {
    // TODO: Implement OCI object download.
    throw new UnsupportedOperationException("OCI object download is not yet implemented.");
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      boolean checkExistsOnAll) {
    // TODO: Implement OCI object existence check.
    return false;
  }

  @Override
  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      AdvancedRestorePreflightParams preflightParams, CustomerConfigData configData) {
    throw ybcNotSupportedException();
  }

  private PlatformServiceException ybcNotSupportedException() {
    return new PlatformServiceException(
        BAD_REQUEST, "OCI storage backups are not yet supported. YBC integration is pending.");
  }

  private UnsupportedOperationException notImplementedException() {
    return new UnsupportedOperationException("OCI storage util logic is not yet implemented.");
  }
}
