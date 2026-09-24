// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import com.google.common.collect.HashMultimap;
import com.google.common.collect.SetMultimap;
import com.google.inject.Singleton;
import com.oracle.bmc.core.model.Subnet;
import com.oracle.bmc.dns.model.Zone;
import com.yugabyte.yw.cloud.oci.OCICloudImpl;
import com.yugabyte.yw.cloud.oci.OCICloudUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.OCICloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.OCIRegionCloudInfo;
import java.util.List;
import java.util.regex.Pattern;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class OCIProviderValidator extends ProviderFieldsValidator {

  private static final Pattern VCN_OCID_PATTERN = Pattern.compile("^ocid1\\.vcn\\..+");
  private static final Pattern SUBNET_OCID_PATTERN = Pattern.compile("^ocid1\\.subnet\\..+");
  private static final Pattern DNS_ZONE_OCID_PATTERN = Pattern.compile("^ocid1\\.dns-zone\\..+");

  private final OCICloudImpl ociCloudImpl;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public OCIProviderValidator(
      BeanValidator beanValidator, RuntimeConfGetter runtimeConfGetter, OCICloudImpl ociCloudImpl) {
    super(beanValidator, runtimeConfGetter);
    this.runtimeConfGetter = runtimeConfGetter;
    this.ociCloudImpl = ociCloudImpl;
  }

  @Override
  public void validate(Provider provider) {
    if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableOciProviderValidation)) {
      log.warn("OCI provider validation is not enabled");
      return;
    }

    if (!ociCloudImpl.isValidCreds(provider)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid OCI credentials or configuration: ensure compartment and region are set,"
              + " and API key or instance principal auth is configured correctly [check logs for"
              + " details]");
    }

    SetMultimap<String, String> validationErrorsMap = HashMultimap.create();

    List<AccessKey> accessKeys = provider.getAllAccessKeys();
    if (CollectionUtils.isNotEmpty(accessKeys)) {
      try {
        validatePrivateKey(accessKeys);
      } catch (PlatformServiceException e) {
        if (e.getHttpStatus() == BAD_REQUEST || e.getHttpStatus() == NOT_FOUND) {
          validationErrorsMap.put("SSH_PRIVATE_KEY_CONTENT", e.getMessage());
        } else {
          throw e;
        }
      }
    }

    if (provider.getDetails() != null && provider.getDetails().ntpServers != null) {
      try {
        validateNTPServers(provider.getDetails().ntpServers);
      } catch (PlatformServiceException e) {
        validationErrorsMap.put("NTP_SERVERS", e.getMessage());
      }
    }

    validateHostedZone(provider, validationErrorsMap);

    if (CollectionUtils.isNotEmpty(provider.getRegions())) {
      for (Region region : provider.getRegions()) {
        OCIRegionCloudInfo regionCloudInfo = CloudInfoInterface.get(region);
        if (regionCloudInfo != null) {
          String instanceTemplate = regionCloudInfo.getInstanceTemplate();
          if (StringUtils.isNotEmpty(instanceTemplate)
              && !OCICloudUtil.isValidInstanceConfigurationOcid(instanceTemplate)) {
            validationErrorsMap.put(
                "instanceTemplate",
                "instanceTemplate must be a valid OCI Instance Configuration OCID");
          }
        }
        validateVcn(provider, region, validationErrorsMap);
        validateSubnets(provider, region, validationErrorsMap);
      }
    }

    if (!validationErrorsMap.isEmpty()) {
      throwMultipleProviderValidatorError(validationErrorsMap, null);
    }
  }

  private void validateHostedZone(
      Provider provider, SetMultimap<String, String> validationErrorsMap) {
    OCICloudInfo cloudInfo = CloudInfoInterface.get(provider);
    String zoneId = cloudInfo == null ? null : cloudInfo.ociHostedZoneId;
    if (StringUtils.isEmpty(zoneId)) {
      return;
    }

    if (!DNS_ZONE_OCID_PATTERN.matcher(zoneId).matches()) {
      validationErrorsMap.put(
          "HOSTED_ZONE",
          "Invalid DNS zone OCID '" + zoneId + "'. Expected format: ocid1.dns-zone...");
      return;
    }

    try {
      Zone zone = ociCloudImpl.getDnsZoneOrBadRequest(provider, zoneId);
      // A VCN's own *.oraclevcn.com zone is protected: OCI rejects every record write and
      // delete on it with 409, so universe create would only fail later at UpdateDnsEntry.
      if (Boolean.TRUE.equals(zone.getIsProtected())) {
        validationErrorsMap.put(
            "HOSTED_ZONE",
            "DNS zone "
                + zone.getName()
                + " is OCI-managed and does not accept record changes. Use a private zone you"
                + " created, in a view attached to the VCN's resolver.");
      }
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST || e.getHttpStatus() == NOT_FOUND) {
        validationErrorsMap.put("HOSTED_ZONE", e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateVcn(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    String vcnId = region.getVnetName();
    if (StringUtils.isEmpty(vcnId)) {
      return;
    }

    String fieldDetails = "REGION." + region.getCode() + ".VCN";
    if (!VCN_OCID_PATTERN.matcher(vcnId).matches()) {
      validationErrorsMap.put(
          fieldDetails, "Invalid VCN OCID '" + vcnId + "'. Expected format: ocid1.vcn...");
      return;
    }

    try {
      ociCloudImpl.getVcnOrBadRequest(provider, region);
    } catch (PlatformServiceException e) {
      if (e.getHttpStatus() == BAD_REQUEST || e.getHttpStatus() == NOT_FOUND) {
        validationErrorsMap.put(fieldDetails, e.getMessage());
      } else {
        throw e;
      }
    }
  }

  private void validateSubnets(
      Provider provider, Region region, SetMultimap<String, String> validationErrorsMap) {
    if (CollectionUtils.isEmpty(region.getZones())) {
      return;
    }

    String regionVcnId = region.getVnetName();
    for (AvailabilityZone zone : region.getZones()) {
      String subnetId = zone.getSubnet();
      if (StringUtils.isEmpty(subnetId)) {
        continue;
      }

      String fieldDetails = "REGION." + region.getCode() + ".ZONE." + zone.getCode() + ".SUBNET";
      if (!SUBNET_OCID_PATTERN.matcher(subnetId).matches()) {
        validationErrorsMap.put(
            fieldDetails,
            "Invalid subnet OCID '" + subnetId + "'. Expected format: ocid1.subnet...");
        continue;
      }

      try {
        Subnet subnet = ociCloudImpl.getSubnetOrBadRequest(provider, region, subnetId);
        if (!StringUtils.isEmpty(regionVcnId)
            && VCN_OCID_PATTERN.matcher(regionVcnId).matches()
            && !regionVcnId.equals(subnet.getVcnId())) {
          validationErrorsMap.put(
              fieldDetails,
              "Subnet "
                  + subnetId
                  + " is not attached to VCN: "
                  + regionVcnId
                  + " (found vcnId: "
                  + subnet.getVcnId()
                  + ")");
        }
      } catch (PlatformServiceException e) {
        if (e.getHttpStatus() == BAD_REQUEST || e.getHttpStatus() == NOT_FOUND) {
          validationErrorsMap.put(fieldDetails, e.getMessage());
        } else {
          throw e;
        }
      }
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    // No OCI-specific availability zone payload checks beyond shared AZ rules.
  }
}
