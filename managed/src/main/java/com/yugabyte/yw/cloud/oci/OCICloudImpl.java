// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
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

      Map<String, String> envVars = provider.getDetails().getCloudInfo().getOci().getEnvVars();

      if (envVars == null || envVars.isEmpty()) {
        log.error("OCI environment variables not configured for provider");
        return false;
      }

      List<String> requiredKeys =
          List.of(
              OCI_TENANCY_ID,
              OCI_USER_ID,
              OCI_FINGERPRINT,
              OCI_COMPARTMENT_ID,
              OCI_REGION,
              OCI_PRIVATE_KEY_CONTENT);
      for (String key : requiredKeys) {
        if (StringUtils.isEmpty(envVars.get(key))) {
          log.error("{} is not configured", key);
          return false;
        }
      }

      if (!validateCredentials(envVars, provider.getName())) {
        return false;
      }

      log.info("OCI credentials validation successful for provider: {}", provider.getName());
      return true;

    } catch (Exception e) {
      log.error("Error validating OCI credentials for provider: {}", provider.getName(), e);
      return false;
    }
  }

  // Performs a live OCI API round-trip to verify that the supplied credentials authenticate and
  // that the compartment OCID is reachable. Uses two cheap Identity API calls:
  //   1) listRegions()          - validates tenancy, user, fingerprint, private key
  //   2) listAvailabilityDomains(compartmentId) - validates the compartment OCID
  private boolean validateCredentials(Map<String, String> envVars, String providerName) {
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
