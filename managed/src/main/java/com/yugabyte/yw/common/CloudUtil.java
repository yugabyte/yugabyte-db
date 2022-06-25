// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.util.List;
import play.api.Play;

public interface CloudUtil {

  public static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;

  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception;

  public boolean canCredentialListObjects(
      CustomerConfigData configData, List<String> storageLocations);

  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception;

  public static <T extends CloudUtil> T getCloudUtil(String configType) {
    switch (configType) {
      case Util.S3:
        return (T) Play.current().injector().instanceOf(AWSUtil.class);
      case Util.GCS:
        return (T) Play.current().injector().instanceOf(GCPUtil.class);
      case Util.AZ:
        return (T) Play.current().injector().instanceOf(AZUtil.class);
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported cloud type");
    }
  }
}
