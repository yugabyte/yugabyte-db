// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.util.Map;
import org.yb.ybc.CloudStoreSpec;
import play.api.Play;

public interface StorageUtil {

  public CloudStoreSpec createCloudStoreSpec(
      String backupLocation, String commonDir, CustomerConfigData configData);

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData);

  public static <T extends StorageUtil> T getStorageUtil(String configType) {
    switch (configType) {
      case Util.S3:
        return (T) Play.current().injector().instanceOf(AWSUtil.class);
      case Util.GCS:
        return (T) Play.current().injector().instanceOf(GCPUtil.class);
      case Util.AZ:
        return (T) Play.current().injector().instanceOf(AZUtil.class);
      case Util.NFS:
        return (T) Play.current().injector().instanceOf(NFSUtil.class);
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported storage type");
    }
  }
}
