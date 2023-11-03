package com.yugabyte.yw.cloud;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeID;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public interface CloudAPI {

  @Singleton
  class Factory {
    Logger LOG = LoggerFactory.getLogger(CloudAPI.class);

    private final Map<String, CloudAPI> cloudAPIMap;

    @Inject
    public Factory(Map<String, CloudAPI> cloudAPIMap) {
      this.cloudAPIMap = cloudAPIMap;
      LOG.info("Created cloud API factory for {}", cloudAPIMap.keySet());
    }

    public CloudAPI get(String code) {
      return cloudAPIMap.get(code);
    }
  }

  /**
   * Check instance offerings by making cloud call for all the regions in azByRegionMap.keySet().
   * Use supplied instanceTypesFilter and availabilityZones (azByRegionMap) as filter for this
   * describe call.
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param azByRegionMap user selected availabilityZone codes by their parent region.
   * @param instanceTypesFilter list of instanceTypes we want to list the offerings for
   * @return a map. Key of this map is instance type like "c5.xlarge" and value is all the
   *     availabilityZones for which the instance type is being offered.
   */
  Map<String, Set<String>> offeredZonesByInstanceType(
      Provider provider, Map<Region, Set<String>> azByRegionMap, Set<String> instanceTypesFilter);

  /**
   * Check whether cloud provider's credentials are valid or not.
   *
   * @param config The credentials info.
   * @return true if credentials are valid otherwise return false.
   */
  boolean isValidCreds(Provider provider, String region);

  /**
   * Check whether cloud provider's credentials are valid to do KMS operations.
   *
   * @param config A JSON object that contains the credentials info.
   * @return true if credentials are valid otherwise return false.
   */
  boolean isValidCredsKms(ObjectNode config, UUID customerUUID);

  void manageNodeGroup(
      Provider provider,
      String regionCode,
      String lbName,
      Map<AvailabilityZone, Set<NodeID>> azToNodesMap,
      List<Integer> ports,
      NLBHealthCheckConfiguration healthCheckConfig);

  void validateInstanceTemplate(Provider provider, String instanceTemplate);

  // Helper function to extract Resource name from resource URL
  // It only works for URls that end with the resource Name.
  static String getResourceNameFromResourceUrl(String resourceUrl) {
    if (resourceUrl != null && !resourceUrl.isEmpty()) {
      String[] urlParts = resourceUrl.split("/", 0);
      return urlParts[urlParts.length - 1];
    }
    return null;
  }
}
