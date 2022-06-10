// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud.azu;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;

public class AZUCloudImpl implements CloudAPI {

  /**
   * Find the instance types offered in availabilityZones.
   *
   * @param provider the cloud provider bean for the AWS provider.
   * @param azByRegionMap user selected availabilityZones by their parent region.
   * @param instanceTypesFilter list of instanceTypes for which we want to list the offerings.
   * @return a map. Key of this map is instance type like "c5.xlarge" and value is all the
   *     availabilityZones for which the instance type is being offered.
   */
  @Override
  public Map<String, Set<String>> offeredZonesByInstanceType(
      Provider provider, Map<Region, Set<String>> azByRegionMap, Set<String> instanceTypesFilter) {
    // TODO make a call to the cloud provider to populate.
    // Make the instances available in all availabilityZones.
    Set<String> azs =
        azByRegionMap.values().stream().flatMap(s -> s.stream()).collect(Collectors.toSet());
    return instanceTypesFilter.stream().collect(Collectors.toMap(Function.identity(), i -> azs));
  }

  // Basic validation to make sure that the credentials work with Azure.
  @Override
  public boolean isValidCreds(Map<String, String> config, String region) {
    // TODO validation for Azure crashes VM at the moment due to netty and jackson version issues.
    return true;
  }

  @Override
  public boolean isValidCredsKms(ObjectNode config, UUID customerUUID) {
    return true;
  }
}
