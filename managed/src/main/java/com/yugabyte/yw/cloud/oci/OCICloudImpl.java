// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.oracle.bmc.auth.AbstractAuthenticationDetailsProvider;
import com.oracle.bmc.auth.InstancePrincipalsAuthenticationDetailsProvider;
import com.oracle.bmc.auth.SimpleAuthenticationDetailsProvider;
import com.oracle.bmc.core.VirtualNetworkClient;
import com.oracle.bmc.core.model.Subnet;
import com.oracle.bmc.core.model.Vcn;
import com.oracle.bmc.core.requests.GetSubnetRequest;
import com.oracle.bmc.core.requests.GetVcnRequest;
import com.oracle.bmc.identity.IdentityClient;
import com.oracle.bmc.identity.requests.ListAvailabilityDomainsRequest;
import com.oracle.bmc.identity.requests.ListRegionsRequest;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import com.yugabyte.yw.models.helpers.provider.OCICloudInfo;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class OCICloudImpl implements CloudAPI {

  public static final String OCI_AUTH_TYPE = "OCI_AUTH_TYPE";
  public static final String OCI_TENANCY_ID = "OCI_TENANCY_ID";
  public static final String OCI_USER_ID = "OCI_USER_ID";
  public static final String OCI_FINGERPRINT = "OCI_FINGERPRINT";
  public static final String OCI_PRIVATE_KEY_CONTENT = "OCI_PRIVATE_KEY_CONTENT";
  public static final String OCI_REGION = "OCI_REGION";
  public static final String OCI_COMPARTMENT_ID = "OCI_COMPARTMENT_ID";

  @Inject private RuntimeConfGetter runtimeConfGetter;

  @Override
  public Map<String, Set<String>> offeredZonesByInstanceType(
      Provider provider, Map<Region, Set<String>> azByRegionMap, Set<String> instanceTypesFilter) {
    Set<String> azs =
        azByRegionMap.values().stream().flatMap(Set::stream).collect(Collectors.toSet());
    return instanceTypesFilter.stream().collect(Collectors.toMap(Function.identity(), i -> azs));
  }

  @Override
  public boolean isValidCreds(Provider provider) {
    try {
      if (provider.getDetails() == null
          || provider.getDetails().getCloudInfo() == null
          || provider.getDetails().getCloudInfo().getOci() == null) {
        log.error("OCI cloud info not configured for provider");
        return false;
      }

      OCICloudInfo ociCloudInfo = provider.getDetails().getCloudInfo().getOci();
      Map<String, String> envVars = ociCloudInfo.getEnvVars();

      if (envVars == null || envVars.isEmpty()) {
        log.error("OCI environment variables not configured for provider");
        return false;
      }

      if (StringUtils.isEmpty(envVars.get(OCI_COMPARTMENT_ID))
          || StringUtils.isEmpty(envVars.get(OCI_REGION))) {
        log.error("OCI_COMPARTMENT_ID and OCI_REGION are required");
        return false;
      }

      if (ociCloudInfo.usesInstancePrincipal()) {
        if (!validateInstancePrincipalCredentials(envVars, provider.getName())) {
          return false;
        }
      } else {
        List<String> requiredApiKeyFields =
            List.of(
                OCI_TENANCY_ID,
                OCI_USER_ID,
                OCI_FINGERPRINT,
                OCI_COMPARTMENT_ID,
                OCI_REGION,
                OCI_PRIVATE_KEY_CONTENT);
        for (String key : requiredApiKeyFields) {
          if (StringUtils.isEmpty(envVars.get(key))) {
            log.error("{} is not configured", key);
            return false;
          }
        }

        if (!validateApiKeyCredentials(envVars, provider.getName())) {
          return false;
        }
      }

      log.info("OCI credentials validation successful for provider: {}", provider.getName());
      return true;

    } catch (Exception e) {
      log.error("Error validating OCI credentials for provider: {}", provider.getName(), e);
      return false;
    }
  }

  /**
   * Looks up a VCN by OCID in the given region. Throws NOT_FOUND when OCI reports the VCN does not
   * exist, and BAD_REQUEST for other lookup failures.
   */
  public Vcn getVcnOrBadRequest(Provider provider, Region region) {
    String vcnId = region.getVnetName();
    try (VirtualNetworkClient networkClient = getVirtualNetworkClient(provider, region.getCode())) {
      return networkClient.getVcn(GetVcnRequest.builder().vcnId(vcnId).build()).getVcn();
    } catch (PlatformServiceException e) {
      throw e;
    } catch (BmcException e) {
      log.error("OCI VCN lookup failed for {}: ", vcnId, e);
      throw wrapLookupFailure(e, "VCN", vcnId);
    } catch (Exception e) {
      log.error("Unexpected error looking up OCI VCN {}: ", vcnId, e);
      throw new PlatformServiceException(
          BAD_REQUEST, "VCN details extraction failed: " + e.getMessage());
    }
  }

  /**
   * Looks up a subnet by OCID in the given region. Throws NOT_FOUND when OCI reports the subnet
   * does not exist, and BAD_REQUEST for other lookup failures.
   */
  public Subnet getSubnetOrBadRequest(Provider provider, Region region, String subnetId) {
    try (VirtualNetworkClient networkClient = getVirtualNetworkClient(provider, region.getCode())) {
      return networkClient
          .getSubnet(GetSubnetRequest.builder().subnetId(subnetId).build())
          .getSubnet();
    } catch (PlatformServiceException e) {
      throw e;
    } catch (BmcException e) {
      log.error("OCI subnet lookup failed for {}: ", subnetId, e);
      throw wrapLookupFailure(e, "Subnet", subnetId);
    } catch (Exception e) {
      log.error("Unexpected error looking up OCI subnet {}: ", subnetId, e);
      throw new PlatformServiceException(
          BAD_REQUEST, "Subnet details extraction failed: " + e.getMessage());
    }
  }

  private static PlatformServiceException wrapLookupFailure(
      BmcException e, String resourceLabel, String resourceId) {
    if (e.getStatusCode() == NOT_FOUND) {
      return new PlatformServiceException(NOT_FOUND, resourceLabel + " not found: " + resourceId);
    }
    return new PlatformServiceException(
        BAD_REQUEST, resourceLabel + " details extraction failed: " + e.getMessage());
  }

  /**
   * Builds a VirtualNetworkClient authenticated with the provider credentials for the given region
   * code. Package-visible for tests.
   */
  VirtualNetworkClient getVirtualNetworkClient(Provider provider, String regionCode) {
    OCICloudInfo ociCloudInfo = requireOciCloudInfo(provider);
    AbstractAuthenticationDetailsProvider authProvider = buildAuthProvider(ociCloudInfo);
    com.oracle.bmc.Region region = resolveRegion(regionCode, provider.getName());
    return VirtualNetworkClient.builder().region(region).build(authProvider);
  }

  // Performs a live OCI API round-trip to verify that the supplied API key credentials
  // authenticate and that the compartment OCID is reachable.
  private boolean validateApiKeyCredentials(Map<String, String> envVars, String providerName) {
    String compartmentId = envVars.get(OCI_COMPARTMENT_ID);
    String regionStr = envVars.get(OCI_REGION);

    com.oracle.bmc.Region region;
    try {
      region = resolveRegion(regionStr, providerName);
    } catch (PlatformServiceException e) {
      log.error(
          "Invalid OCI region '{}' for provider {}: {}", regionStr, providerName, e.getMessage());
      return false;
    }

    return validateIdentityAccess(
        buildApiKeyAuthProvider(envVars), region, compartmentId, providerName);
  }

  private boolean validateInstancePrincipalCredentials(
      Map<String, String> envVars, String providerName) {
    String compartmentId = envVars.get(OCI_COMPARTMENT_ID);
    String regionStr = envVars.get(OCI_REGION);

    com.oracle.bmc.Region region;
    try {
      region = resolveRegion(regionStr, providerName);
    } catch (PlatformServiceException e) {
      log.error(
          "Invalid OCI region '{}' for provider {}: {}", regionStr, providerName, e.getMessage());
      return false;
    }

    AbstractAuthenticationDetailsProvider authProvider =
        InstancePrincipalsAuthenticationDetailsProvider.builder().build();

    return validateIdentityAccess(authProvider, region, compartmentId, providerName);
  }

  private boolean validateIdentityAccess(
      AbstractAuthenticationDetailsProvider authProvider,
      com.oracle.bmc.Region region,
      String compartmentId,
      String providerName) {
    try (IdentityClient identityClient =
        IdentityClient.builder().region(region).build(authProvider)) {
      identityClient.listRegions(ListRegionsRequest.builder().build());

      identityClient.listAvailabilityDomains(
          ListAvailabilityDomainsRequest.builder().compartmentId(compartmentId).build());

      return true;
    } catch (BmcException e) {
      log.error(
          "OCI live credential validation failed for provider {} (HTTP {}, serviceCode={}): {}",
          providerName,
          e.getStatusCode(),
          e.getServiceCode(),
          e.getMessage());
      return false;
    } catch (Exception e) {
      log.error(
          "Unexpected error during OCI live credential validation for provider {}",
          providerName,
          e);
      return false;
    }
  }

  private AbstractAuthenticationDetailsProvider buildAuthProvider(OCICloudInfo ociCloudInfo) {
    if (ociCloudInfo.usesInstancePrincipal()) {
      return InstancePrincipalsAuthenticationDetailsProvider.builder().build();
    }
    return buildApiKeyAuthProvider(ociCloudInfo.getEnvVars());
  }

  private SimpleAuthenticationDetailsProvider buildApiKeyAuthProvider(Map<String, String> envVars) {
    String tenancyId = envVars.get(OCI_TENANCY_ID);
    String userId = envVars.get(OCI_USER_ID);
    String fingerprint = envVars.get(OCI_FINGERPRINT);
    String privateKeyContent = envVars.get(OCI_PRIVATE_KEY_CONTENT);
    return SimpleAuthenticationDetailsProvider.builder()
        .tenantId(tenancyId)
        .userId(userId)
        .fingerprint(fingerprint)
        .privateKeySupplier(
            () -> new ByteArrayInputStream(privateKeyContent.getBytes(StandardCharsets.UTF_8)))
        .build();
  }

  private OCICloudInfo requireOciCloudInfo(Provider provider) {
    if (provider.getDetails() == null
        || provider.getDetails().getCloudInfo() == null
        || provider.getDetails().getCloudInfo().getOci() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "OCI cloud info not configured for provider");
    }
    return provider.getDetails().getCloudInfo().getOci();
  }

  private com.oracle.bmc.Region resolveRegion(String regionStr, String providerName) {
    try {
      return com.oracle.bmc.Region.fromRegionId(regionStr);
    } catch (IllegalArgumentException e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Invalid OCI region '%s' for provider %s: %s",
              regionStr, providerName, e.getMessage()));
    }
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }

  @Override
  public void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodeIDs,
      List<Integer> ports,
      NLBHealthCheckConfiguration healthCheckConfig) {
    throw new UnsupportedOperationException("OCI load balancer management is not yet supported");
  }
}
