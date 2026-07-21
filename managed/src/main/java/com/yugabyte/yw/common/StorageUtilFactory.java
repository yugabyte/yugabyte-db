package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;

@Singleton
public class StorageUtilFactory extends CloudUtilFactory {
  private final NFSUtil nfsUtil;

  @Inject
  public StorageUtilFactory(
      AWSUtil awsUtil, NFSUtil nfsUtil, GCPUtil gcpUtil, AZUtil azUtil, OCIUtil ociUtil) {
    super(awsUtil, gcpUtil, azUtil, ociUtil);
    this.nfsUtil = nfsUtil;
  }

  public StorageUtil getStorageUtil(String configType) {
    switch (configType) {
      case Util.S3:
      case Util.AZ:
      case Util.GCS:
      case Util.OCI:
        return getCloudUtil(configType);
      case Util.NFS:
        return nfsUtil;
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported storage type");
    }
  }
}
