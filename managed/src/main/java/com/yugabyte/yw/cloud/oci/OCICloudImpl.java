// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.oracle.bmc.auth.AbstractAuthenticationDetailsProvider;
import com.oracle.bmc.auth.InstancePrincipalsAuthenticationDetailsProvider;
import com.oracle.bmc.auth.SimpleAuthenticationDetailsProvider;
import com.oracle.bmc.identity.IdentityClient;
import com.oracle.bmc.identity.requests.ListAvailabilityDomainsRequest;
import com.oracle.bmc.identity.requests.ListRegionsRequest;
import com.oracle.bmc.model.BmcException;
import com.yugabyte.yw.cloud.CloudAPI;
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

  // Performs a live OCI API round-trip to verify that the supplied API key credentials
  // authenticate and that the compartment OCID is reachable.
  private boolean validateApiKeyCredentials(Map<String, String> envVars, String providerName) {
    String tenancyId = envVars.get(OCI_TENANCY_ID);
    String userId = envVars.get(OCI_USER_ID);
    String fingerprint = envVars.get(OCI_FINGERPRINT);
    String privateKeyContent = envVars.get(OCI_PRIVATE_KEY_CONTENT);
    String compartmentId = envVars.get(OCI_COMPARTMENT_ID);
    String regionStr = envVars.get(OCI_REGION);

    com.oracle.bmc.Region region;
    try {
      region = com.oracle.bmc.Region.fromRegionId(regionStr);
    } catch (IllegalArgumentException e) {
      log.error(
          "Invalid OCI region '{}' for provider {}: {}", regionStr, providerName, e.getMessage());
      return false;
    }

    SimpleAuthenticationDetailsProvider authProvider =
        SimpleAuthenticationDetailsProvider.builder()
            .tenantId(tenancyId)
            .userId(userId)
            .fingerprint(fingerprint)
            .privateKeySupplier(
                () -> new ByteArrayInputStream(privateKeyContent.getBytes(StandardCharsets.UTF_8)))
            .build();

    return validateIdentityAccess(authProvider, region, compartmentId, providerName);
  }

  private boolean validateInstancePrincipalCredentials(
      Map<String, String> envVars, String providerName) {
    String compartmentId = envVars.get(OCI_COMPARTMENT_ID);
    String regionStr = envVars.get(OCI_REGION);

    com.oracle.bmc.Region region;
    try {
      region = com.oracle.bmc.Region.fromRegionId(regionStr);
    } catch (IllegalArgumentException e) {
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
