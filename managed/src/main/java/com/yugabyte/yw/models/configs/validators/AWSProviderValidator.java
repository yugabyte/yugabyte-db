// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.amazonaws.services.ec2.model.Image;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;

@Singleton
public class AWSProviderValidator extends ProviderFieldsValidator {

  private final AWSCloudImpl awsCloudImpl;
  private final RuntimeConfGetter runtimeConfigGetter;

  @Inject
  public AWSProviderValidator(
      BeanValidator beanValidator,
      AWSCloudImpl awsCloudImpl,
      RuntimeConfGetter runtimeConfigGetter) {
    super(beanValidator, runtimeConfigGetter);
    this.awsCloudImpl = awsCloudImpl;
    this.runtimeConfigGetter = runtimeConfigGetter;
  }

  @Override
  public void validate(Provider provider) {
    Map<String, String> configMap = CloudInfoInterface.fetchEnvVars(provider);

    // validate Access and secret keys
    try {
      if (checkKeysExists(configMap)) {
        awsCloudImpl.getStsClientOrBadRequest(configMap);
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        throwBeanValidatorError("KEYS", e.getMessage());
      }
      throw e;
    }

    // validate SSH private key content
    try {
      if (provider.allAccessKeys != null && provider.allAccessKeys.size() > 0) {
        for (AccessKey accessKey : provider.allAccessKeys) {
          String privateKeyContent = accessKey.getKeyInfo().sshPrivateKeyContent;
          if (!awsCloudImpl.getPrivateKeyOrBadRequest(privateKeyContent).equals("RSA")) {
            throwBeanValidatorError("SSH_PRIVATE_KEY_CONTENT", "Please provide a valid RSA key");
          }
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        throwBeanValidatorError("SSH_PRIVATE_KEY_CONTENT", e.getMessage());
      }
      throw e;
    }

    // validate NTP Servers
    if (provider.details != null && provider.details.ntpServers != null) {
      validateNTPServers(provider.details.ntpServers);
    }

    // validate hosted zone id
    try {
      String hostedZoneId = provider.details.cloudInfo.aws.awsHostedZoneId;
      if (checkKeysExists(configMap) && !StringUtils.isEmpty(hostedZoneId)) {
        awsCloudImpl.getHostedZoneOrBadRequest(configMap, hostedZoneId);
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        throwBeanValidatorError("HOSTED_ZONE", e.getMessage());
      }
      throw e;
    }

    // validate AMI details
    if (checkKeysExists(configMap)) {
      for (Region region : provider.regions) {
        String imageId = region.getYbImage();
        String fieldDetails = "REGION." + region.code + "." + imageId;
        try {
          if (!StringUtils.isEmpty(imageId)) {
            Image image = awsCloudImpl.describeImageOrBadRequest(provider, region, imageId);
            String arch = image.getArchitecture().toLowerCase();
            List<String> supportedArch =
                runtimeConfigGetter.getStaticConf().getStringList("yb.aws.supported_arch_types");
            if (!supportedArch.contains(arch)) {
              throwBeanValidatorError(
                  fieldDetails, arch + " arch on image " + imageId + " is not supported");
            }
            List<String> supportedRootDeviceType =
                runtimeConfigGetter
                    .getStaticConf()
                    .getStringList("yb.aws.supported_root_device_type");
            String rootDeviceType = image.getRootDeviceType().toLowerCase();
            if (!supportedRootDeviceType.contains(rootDeviceType)) {
              throwBeanValidatorError(
                  fieldDetails,
                  rootDeviceType + " root device type on image " + imageId + " is not supported");
            }
            List<String> supportedPlatform =
                runtimeConfigGetter.getStaticConf().getStringList("yb.aws.supported_platform");
            String platformDetails = image.getPlatformDetails().toLowerCase();
            if (!supportedPlatform
                .stream()
                .anyMatch(platform -> platformDetails.contains(platform))) {
              throwBeanValidatorError(
                  fieldDetails,
                  platformDetails + " platform on image " + imageId + " is not supported");
            }
          }
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == BAD_REQUEST) {
            throwBeanValidatorError(fieldDetails, e.getMessage());
          }
          throw e;
        }
      }
    }

    // dry run DescribeInstances
    if (checkKeysExists(configMap)) {
      for (Region region : provider.regions) {
        String fieldDetails = "DRY_RUN." + region.code;
        try {
          awsCloudImpl.dryRunDescribeInstanceOrBadRequest(configMap, region.code);
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == BAD_REQUEST) {
            throwBeanValidatorError(fieldDetails, e.getMessage());
          }
          throw e;
        }
      }
    }
  }

  private boolean checkKeysExists(Map<String, String> configMap) {
    return configMap != null
        && !StringUtils.isEmpty(configMap.get("AWS_ACCESS_KEY_ID"))
        && !StringUtils.isEmpty(configMap.get("AWS_SECRET_ACCESS_KEY"));
  }
}
