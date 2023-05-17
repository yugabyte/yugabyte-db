// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.io.InputStream;
import java.nio.file.Path;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;

public interface CloudUtil extends StorageUtil {

  public static enum ExtraPermissionToValidate {
    READ,
    LIST,
    DELETE,
    NULL
  }

  public static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;
  public static final String SUCCESS = "success";
  int FILE_DOWNLOAD_BUFFER_SIZE = 8 * 1024;

  public static final String DUMMY_DATA = "dummy-text";

  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception;

  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception;

  public <T> T listBuckets(CustomerConfigData configData);

  public InputStream getCloudFileInputStream(CustomerConfigData configData, String cloudPath)
      throws Exception;

  default void downloadCloudFile(CustomerConfigData configData, String cloudPath, Path destination)
      throws Exception {
    FileUtils.copyInputStreamToFile(
        getCloudFileInputStream(configData, cloudPath), destination.toFile());
  }

  public static <T extends CloudUtil> T getCloudUtil(String configType) {
    switch (configType) {
      case Util.S3:
        return (T) StaticInjectorHolder.injector().instanceOf(AWSUtil.class);
      case Util.GCS:
        return (T) StaticInjectorHolder.injector().instanceOf(GCPUtil.class);
      case Util.AZ:
        return (T) StaticInjectorHolder.injector().instanceOf(AZUtil.class);
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported cloud type");
    }
  }

  default void validate(CustomerConfigData configData, List<ExtraPermissionToValidate> permissions)
      throws Exception {
    // default fall through stub
  }

  default UUID getRandomUUID() {
    return UUID.randomUUID();
  }
}
