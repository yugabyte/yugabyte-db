// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.amazonaws.services.ec2.model.Image;
import com.amazonaws.services.ec2.model.IpPermission;
import com.amazonaws.services.ec2.model.SecurityGroup;
import com.amazonaws.services.ec2.model.Subnet;
import com.amazonaws.util.CollectionUtils;
import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Singleton;
import com.yugabyte.yw.cloud.aws.AWSCloudImpl;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface.VPCType;
import com.yugabyte.yw.models.helpers.provider.AWSCloudInfo;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
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
    // To guarantee that we can safely fall back to the default provider,
    // the user should either submit both keys and their secret or leave them as null.
    checkMissingKeys(provider);

    // validate access
    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      for (Region region : provider.getRegions()) {
        try {
          awsCloudImpl.getStsClientOrBadRequest(provider, region);
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == BAD_REQUEST) {
            if (awsCloudImpl.checkKeysExists(provider)) {
              throwBeanProviderValidatorError("KEYS", e.getMessage());
            } else {
              throwBeanProviderValidatorError("IAM", e.getMessage());
            }
          }
          throw e;
        }
      }
    }

    // Collect validation errors only when client is successfully created, otherwise,
    // all other validations will unquestionably fail.
    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();

    // validate SSH private key content
    try {
      if (provider.getAllAccessKeys() != null && provider.getAllAccessKeys().size() > 0) {
        for (AccessKey accessKey : provider.getAllAccessKeys()) {
          String privateKeyContent = accessKey.getKeyInfo().sshPrivateKeyContent;
          if (!awsCloudImpl.getPrivateKeyAlgoOrBadRequest(privateKeyContent).equals("RSA")) {
            throw new PlatformServiceException(BAD_REQUEST, "Please provide a valid RSA key");
          }
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put("SSH_PRIVATE_KEY_CONTENT", e.getMessage());
      } else {
        throw e;
      }
    }

    // validate NTP Servers
    if (provider.getDetails() != null && provider.getDetails().ntpServers != null) {
      try {
        validateNTPServers(provider.getDetails().ntpServers);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put("NTP_SERVERS", e.getMessage());
      }
    }

    if (provider.getDetails().sshPort == null) {
      validationErrorsMap.put("SSH_PORT", "Please provide a valid ssh port value");
    }

    // validate hosted zone id
    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      for (Region region : provider.getRegions()) {
        try {
          String hostedZoneId = provider.getDetails().getCloudInfo().aws.awsHostedZoneId;
          if (!StringUtils.isEmpty(hostedZoneId)) {
            awsCloudImpl.getHostedZoneOrBadRequest(provider, region, hostedZoneId);
          }
        } catch (PlatformServiceException e) {
          if (e.getHttpStatus() == BAD_REQUEST) {
            validationErrorsMap.put("HOSTED_ZONE", e.getMessage());
          } else {
            throw e;
          }
        }
      }
    }

    // validate Region and its details
    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      for (Region region : provider.getRegions()) {
        validateAMI(provider, region, validationErrorsMap);
        validateVpc(provider, region, validationErrorsMap);
        validateSgAndPort(provider, region, validationErrorsMap);
        validateSubnets(provider, region, validationErrorsMap);
        dryRun(provider, region, validationErrorsMap);
      }
    }

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap);
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // pass
  }

  private void dryRun(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String fieldDetails = "REGION." + region.getCode() + ".DRY_RUN";
    try {
      AWSCloudInfo cloudInfo = provider.getDetails().getCloudInfo().getAws();
      List<DryRunValidators> dryRunValidators = new ArrayList<>();
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunDescribeInstanceOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunDescribeImageOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunDescribeInstanceTypesOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunDescribeVpcsOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunDescribeSubnetOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunSecurityGroupOrBadRequest(provider, region.getCode()));
      dryRunValidators.add(
          () -> awsCloudImpl.dryRunKeyPairOrBadRequest(provider, region.getCode()));
      if (cloudInfo.getVpcType() != null && cloudInfo.getVpcType().equals(VPCType.NEW)) {
        dryRunValidators.add(
            () ->
                awsCloudImpl.dryRunAuthorizeSecurityGroupIngressOrBadRequest(
                    provider, region.getCode()));
      }
      // Run each dry run validators.
      for (DryRunValidators dryRunValidator : dryRunValidators) {
        try {
          dryRunValidator.execute();
        } catch (PlatformServiceException e) {
          validationErrorsMap.put(fieldDetails, e.getMessage());
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateAMI(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String imageId = region.getYbImage();
    String fieldDetails = "REGION." + region.getCode() + ".IMAGE";
    try {
      if (!StringUtils.isEmpty(imageId)) {
        Image image = awsCloudImpl.describeImageOrBadRequest(provider, region, imageId);
        List<String> errorList = new ArrayList<>();
        String arch = image.getArchitecture().toLowerCase();
        List<String> supportedArch =
            runtimeConfigGetter.getStaticConf().getStringList("yb.aws.supported_arch_types");
        if (!supportedArch.contains(arch)) {
          errorList.add(arch + " arch on image " + imageId + " is not supported");
        }
        List<String> supportedRootDeviceType =
            runtimeConfigGetter.getStaticConf().getStringList("yb.aws.supported_root_device_type");
        String rootDeviceType = image.getRootDeviceType().toLowerCase();
        if (!supportedRootDeviceType.contains(rootDeviceType)) {
          errorList.add(
              rootDeviceType + " root device type on image " + imageId + " is not supported");
        }
        List<String> supportedPlatform =
            runtimeConfigGetter.getStaticConf().getStringList("yb.aws.supported_platform");
        String platformDetails = image.getPlatformDetails().toLowerCase();
        if (supportedPlatform.stream().noneMatch(platformDetails::contains)) {
          errorList.add(platformDetails + " platform on image " + imageId + " is not supported");
        }
        if (errorList.size() != 0) {
          validationErrorsMap.putAll(fieldDetails, errorList);
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateVpc(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String fieldDetails = "REGION." + region.getCode() + ".VPC";
    try {
      if (!StringUtils.isEmpty(region.getVnetName())) {
        awsCloudImpl.describeVpcOrBadRequest(provider, region);
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateSgAndPort(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String fieldDetails = "REGION." + region.getCode() + ".SECURITY_GROUP";

    try {
      if (!StringUtils.isEmpty(region.getSecurityGroupId())) {
        List<SecurityGroup> securityGroupList =
            awsCloudImpl.describeSecurityGroupsOrBadRequest(provider, region);
        List<String> errorList = new ArrayList<>();
        for (SecurityGroup securityGroup : securityGroupList) {
          if (StringUtils.isEmpty(securityGroup.getVpcId())) {
            errorList.add("No vpc is attached to SG: " + securityGroup.getGroupId());
          } else if (!securityGroup.getVpcId().equals(region.getVnetName())) {
            errorList.add(
                securityGroup.getGroupId() + " is not attached to vpc: " + region.getVnetName());
          }
          Integer sshPort = provider.getDetails().getSshPort();
          boolean portOpen = false;
          if (!CollectionUtils.isNullOrEmpty(securityGroup.getIpPermissions())) {
            for (IpPermission ipPermission : securityGroup.getIpPermissions()) {
              Integer fromPort = ipPermission.getFromPort();
              Integer toPort = ipPermission.getToPort();
              if (fromPort == null && toPort == null) {
                portOpen = true;
                break;
              }
              if (fromPort == null || toPort == null) {
                continue;
              }
              if (fromPort <= sshPort && toPort >= sshPort) {
                portOpen = true;
                break;
              }
            }
          }
          if (!portOpen) {
            errorList.add(sshPort + " is not open on security group " + securityGroup.getGroupId());
          }
        }
        if (errorList.size() != 0) {
          validationErrorsMap.putAll(fieldDetails, errorList);
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateSubnets(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String fieldDetails = "REGION." + region.getCode() + ".SUBNETS";
    String regionVnetName = region.getVnetName();
    try {
      if (!StringUtils.isEmpty(region.getSecurityGroupId())) {
        List<Subnet> subnets = awsCloudImpl.describeSubnetsOrBadRequest(provider, region);
        List<String> errorList = new ArrayList<>();
        Set<String> cidrBlocks = new HashSet<>();
        for (Subnet subnet : subnets) {
          AvailabilityZone az = getAzBySubnetFromRegion(region, subnet.getSubnetId());
          if (!az.getCode().equals(subnet.getAvailabilityZone())) {
            errorList.add("Invalid AZ code for subnet: " + subnet.getSubnetId());
          }
          if (!subnet.getVpcId().equals(regionVnetName)) {
            errorList.add(subnet.getSubnetId() + " is not associated with " + regionVnetName);
          }
          if (cidrBlocks.contains(subnet.getCidrBlock())) {
            errorList.add("Please provide non-overlapping CIDR blocks subnets");
          }
          cidrBlocks.add(subnet.getCidrBlock());
        }
        if (errorList.size() != 0) {
          validationErrorsMap.putAll(fieldDetails, errorList);
        }
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void checkMissingKeys(Provider provider) {
    AWSCloudInfo cloudInfo = provider.getDetails().getCloudInfo().getAws();
    String accessKey = cloudInfo.awsAccessKeyID;
    String accessKeySecret = cloudInfo.awsAccessKeySecret;
    if ((StringUtils.isEmpty(accessKey) && !StringUtils.isEmpty(accessKeySecret))
        || (!StringUtils.isEmpty(accessKey) && StringUtils.isEmpty(accessKeySecret))) {
      throwBeanProviderValidatorError("KEYS", "Please provide both access key and its secret");
    }
  }

  private AvailabilityZone getAzBySubnetFromRegion(Region region, String subnet) {
    return region.getZones().stream()
        .filter(zone -> zone.getSubnet().equals(subnet))
        .findFirst()
        .orElseThrow(
            () ->
                new PlatformServiceException(
                    BAD_REQUEST, "Could not find AZ for subnet: " + subnet));
  }

  @FunctionalInterface
  interface DryRunValidators {
    void execute();
  }
}
