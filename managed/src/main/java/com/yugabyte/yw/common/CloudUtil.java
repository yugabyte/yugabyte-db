// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.util.List;
import org.yb.ybc.CloudStoreSpec;
import play.api.Play;

public interface CloudUtil extends StorageUtil {

  public static final String KEY_LOCATION_SUFFIX = Util.KEY_LOCATION_SUFFIX;
  public static final String SUCCESS = "success";

  public void deleteKeyIfExists(CustomerConfigData configData, String defaultBackupLocation)
      throws Exception;

  public boolean canCredentialListObjects(
      CustomerConfigData configData, List<String> storageLocations);

  public void deleteStorage(CustomerConfigData configData, List<String> backupLocations)
      throws Exception;

  public <T> T listBuckets(CustomerConfigData configData);

  // public JsonNode readFileFromCloud(String location, CustomerConfigData configData)
  //     throws Exception;

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
