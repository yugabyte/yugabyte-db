package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;

@Singleton
public class CloudUtilFactory {
  private final AWSUtil awsUtil;
  private final GCPUtil gcpUtil;
  private final AZUtil azUtil;

  @Inject
  public CloudUtilFactory(AWSUtil awsUtil, GCPUtil gcpUtil, AZUtil azUtil) {
    this.awsUtil = awsUtil;
    this.gcpUtil = gcpUtil;
    this.azUtil = azUtil;
  }

  public CloudUtil getCloudUtil(String configType) {
    switch (configType) {
      case Util.S3:
        return awsUtil;
      case Util.GCS:
        return gcpUtil;
      case Util.AZ:
        return azUtil;
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Unsupported storage type");
    }
  }
}
