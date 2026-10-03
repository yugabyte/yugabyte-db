package com.yugabyte.yw.cloud;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeID;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
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
  boolean isValidCreds(Provider provider);

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

  // AWS-specific methods with default unsupported implementations
  /**
   * Creates a capacity reservation (AWS-specific feature)
   *
   * @throws UnsupportedOperationException if the cloud provider doesn't support capacity
   *     reservations
   */
  default String createCapacityReservation(
      Provider provider,
      String reservationName,
      String regionCode,
      String availabilityZone,
      String instanceType,
      int count,
      Map<String, String> tags) {
    throw new UnsupportedOperationException(
        "Capacity reservations are not supported by this cloud provider");
  }

  /**
   * Deletes a capacity reservation (AWS-specific feature)
   *
   * @throws UnsupportedOperationException if the cloud provider doesn't support capacity
   *     reservations
   */
  default void deleteCapacityReservation(
      Provider provider, String regionCode, String capacityReservationId) {
    throw new UnsupportedOperationException(
        "Capacity reservations are not supported by this cloud provider");
  }

  /**
   * Current instance type and data-disk IOPS/throughput/size for a node, plus the latest
   * disk-modify start when the cloud records one. Boot/root disks are excluded (same volume set a
   * resize would modify). Volumes that disagree on IOPS, throughput, or size fail closed rather
   * than picking min/max. {@code lastModificationStart} is the latest start across those data
   * volumes, {@link Instant#EPOCH} when AWS has never modified the volume, or null when the cloud
   * has no such API (Azure).
   *
   * <p>Default is empty. Callers that need a cooldown or persist-abort decision must treat empty as
   * unverified, not as "already matches".
   */
  default Optional<NodeDiskSpec> describeNodeDataDiskSpec(Provider provider, NodeDetails node) {
    return Optional.empty();
  }

  /** Aggregated instance + data-disk snapshot for one node. */
  @Getter
  class NodeDiskSpec {
    private final String instanceType;
    private final Integer diskIops;
    private final Integer throughput;
    private final Integer volumeSizeGb;
    private final Instant lastModificationStart;

    public NodeDiskSpec(Integer diskIops, Integer throughput, Instant lastModificationStart) {
      this(null, diskIops, throughput, null, lastModificationStart);
    }

    public NodeDiskSpec(
        String instanceType,
        Integer diskIops,
        Integer throughput,
        Integer volumeSizeGb,
        Instant lastModificationStart) {
      this.instanceType = instanceType;
      this.diskIops = diskIops;
      this.throughput = throughput;
      this.volumeSizeGb = volumeSizeGb;
      this.lastModificationStart = lastModificationStart;
    }

    /**
     * One snapshot for a node's data disks, with {@code instanceType} attached. Disks must agree on
     * IOPS, throughput, and size. {@code lastModificationStart} is the latest non-null start.
     */
    public static NodeDiskSpec mergeDataDisks(String instanceType, List<NodeDiskSpec> disks) {
      if (disks == null || disks.isEmpty()) {
        throw new PlatformServiceException(BAD_REQUEST, "node has no data disks");
      }
      Integer iops = disks.get(0).diskIops;
      Integer throughput = disks.get(0).throughput;
      Integer volumeSizeGb = disks.get(0).volumeSizeGb;
      Instant latest = disks.get(0).lastModificationStart;
      for (int i = 1; i < disks.size(); i++) {
        NodeDiskSpec disk = disks.get(i);
        if (!Objects.equals(iops, disk.diskIops)
            || !Objects.equals(throughput, disk.throughput)
            || !Objects.equals(volumeSizeGb, disk.volumeSizeGb)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "data disks disagree on IOPS, throughput, or size");
        }
        if (disk.lastModificationStart != null
            && (latest == null || disk.lastModificationStart.isAfter(latest))) {
          latest = disk.lastModificationStart;
        }
      }
      return new NodeDiskSpec(instanceType, iops, throughput, volumeSizeGb, latest);
    }
  }

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
