// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import io.swagger.annotations.ApiModelProperty;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import javax.annotation.Nullable;
import javax.validation.constraints.NotNull;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.experimental.SuperBuilder;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

/**
 * Hierarchical node specification.
 *
 * <p>Node properties (instance type, device info, cgroup size, etc.) can be defined at the root
 * level and overridden per region and per availability zone. Specifications are merged from root
 * down to the most specific level when resolving the effective spec for a node.
 */
@Slf4j
public class HierarchicalNodesSpec {

  /** Per-process node properties shared across all hierarchy levels. */
  @Data
  @Builder(toBuilder = true)
  public static class NodeSpec {
    @Builder.Default @ApiModelProperty private DeviceInfo deviceInfo = null;
    @Builder.Default @ApiModelProperty private String instanceType = null;
    @Builder.Default @ApiModelProperty private Integer cgroupSize = null;
    @Builder.Default @ApiModelProperty private ProxyConfig backupProxyConfig = null;

    @Builder.Default @ApiModelProperty
    private UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec k8SNodeResourceSpec = null;

    @JsonIgnore
    public boolean isEmpty() {
      return (deviceInfo == null || deviceInfo.allNull())
          && instanceType == null
          && cgroupSize == null
          && k8SNodeResourceSpec == null
          && backupProxyConfig == null;
    }

    /**
     * Merges non-null fields from this spec into {@code other}. Fields on {@code other} are
     * overwritten when this spec has a value; device info is deep-merged.
     *
     * @param other target spec to update
     */
    public void mergeInto(@NotNull NodeSpec other) {
      if (instanceType != null) {
        other.instanceType = instanceType;
      }
      if (deviceInfo != null) {
        other.deviceInfo =
            UniverseDefinitionTaskParams.UserIntent.mergeDeviceInfos(other.deviceInfo, deviceInfo);
      }
      if (cgroupSize != null) {
        other.cgroupSize = cgroupSize;
      }
      if (k8SNodeResourceSpec != null) {
        other.k8SNodeResourceSpec = k8SNodeResourceSpec;
      }
      if (backupProxyConfig != null) {
        // Just replacing.
        other.backupProxyConfig = backupProxyConfig.clone();
      }
    }

    @JsonIgnore
    public NodeSpec clone() {
      NodeSpec result = NodeSpec.builder().build();
      mergeInto(result);
      return result;
    }

    public static NodeSpec empty() {
      return builder().build();
    }
  }

  /** Tserver and master node specs at a single level of the hierarchy. */
  @Data
  @SuperBuilder(toBuilder = true)
  @NoArgsConstructor
  @AllArgsConstructor
  public static class NodesSpec {
    @ApiModelProperty private NodeSpec tserverSpecification;
    @ApiModelProperty private NodeSpec masterSpecification;

    /**
     * Merges tserver and master specs from this level into {@code other}.
     *
     * @param other target spec to update
     */
    public void mergeInto(@NotNull NodesSpec other) {
      if (tserverSpecification != null) {
        tserverSpecification.mergeInto(other.getOrCreateTserverSpec());
      }
      if (masterSpecification != null) {
        masterSpecification.mergeInto(other.getOrCreateMasterSpec());
      }
    }

    @JsonIgnore
    public NodeSpec getOrCreateTserverSpec() {
      tserverSpecification = emptyIfNull(tserverSpecification);
      return tserverSpecification;
    }

    @JsonIgnore
    public NodeSpec getOrCreateMasterSpec() {
      masterSpecification = emptyIfNull(masterSpecification);
      return masterSpecification;
    }

    @JsonIgnore
    public boolean isEmpty() {
      clean();
      return (tserverSpecification == null || tserverSpecification.isEmpty())
          && (masterSpecification == null || masterSpecification.isEmpty());
    }

    public void clean() {
      if (tserverSpecification != null && tserverSpecification.isEmpty()) {
        tserverSpecification = null;
      }
      if (masterSpecification != null && masterSpecification.isEmpty()) {
        masterSpecification = null;
      }
    }
  }

  /** A node in the root / region / AZ specification hierarchy. */
  public interface HiararchicalNode {

    @JsonIgnore
    NodesSpec getNodesSpec();

    boolean matches(TraversePath traversePath);

    TraversePath updateTraversePath(TraversePath traversePath);

    @JsonIgnore
    default boolean isEmpty() {
      NodesSpec nodesSpec = getNodesSpec();
      return nodesSpec == null || nodesSpec.isEmpty();
    }

    void clean();

    @JsonIgnore
    default String toStr() {
      String result = getClass().getSimpleName();
      if (this instanceof HasCode) {
        result = result + "(" + ((HasCode) this).getCode() + ")";
      }
      return result;
    }

    default void fail(String error) {
      throw new PlatformServiceException(BAD_REQUEST, toStr() + ": " + error);
    }

    // No additional validation by default.
    default void validate() {}
  }

  public interface HasCode {
    String getCode();
  }

  /** A hierarchy node that may have child nodes identified by region or AZ code. */
  public interface WithDescendants<T extends HiararchicalNode & HasCode> extends HiararchicalNode {

    void initDescendants();

    @JsonIgnore
    List<T> getDescendants();

    default void clearEmptyDescendants() {
      List<T> descendants = getDescendants();
      if (descendants != null) {
        Iterator<T> it = descendants.iterator();
        while (it.hasNext()) {
          T descendant = it.next();
          descendant.clean();
          if (descendant instanceof WithDescendants<?>) {
            ((WithDescendants<?>) descendant).clearEmptyDescendants();
          }
          if (descendant.isEmpty()) {
            log.debug("Removing " + descendant);
            it.remove();
          }
        }
      }
    }

    default T getOrCreateDescendant(String childCode) {
      if (StringUtils.isEmpty(childCode)) {
        fail("empty codes are not allowed");
      }
      initDescendants();
      for (T child : getDescendants()) {
        if (Objects.equals(child.getCode(), childCode)) {
          return child;
        }
      }
      T created = initNewDescendant(childCode);
      getDescendants().add(created);
      return created;
    }

    default void addNewDescendant(T newOne) {
      if (newOne == null) {
        fail("Attempt to add a null child");
      }
      if (StringUtils.isEmpty(newOne.getCode())) {
        fail("empty code is not allowed");
      }
      String code = newOne.getCode();
      initDescendants();
      for (T child : getDescendants()) {
        if (Objects.equals(child.getCode(), code)) {
          fail("child with a code " + code + " already exists");
        }
      }
      getDescendants().add(newOne);
    }

    T initNewDescendant(String childCode);

    @Override
    default boolean isEmpty() {
      return CollectionUtils.isEmpty(getDescendants()) && HiararchicalNode.super.isEmpty();
    }

    default void validateDescendants() {
      List<T> descendants = getDescendants();
      if (descendants == null) {
        return;
      }
      Set<String> codes = new HashSet<>();
      for (T descendant : descendants) {
        if (descendant == null) {
          fail("null children are not allowed");
        }
        String code = descendant.getCode();
        if (StringUtils.isEmpty(code)) {
          fail("empty code is not allowed");
        }
        if (!codes.add(code)) {
          fail("found duplicate code: " + code);
        }
        descendant.validate();
      }
    }
  }

  /** Root-level node specification, optionally containing per-region overrides. */
  @Data
  @SuperBuilder(toBuilder = true)
  @NoArgsConstructor
  @AllArgsConstructor
  public static class RootNodesSpec extends NodesSpec implements WithDescendants<RegionNodesSpec> {
    @ApiModelProperty private List<RegionNodesSpec> regionNodesSpecs;

    @Override
    public void initDescendants() {
      if (regionNodesSpecs == null) {
        regionNodesSpecs = new ArrayList<>();
      }
    }

    @Override
    public RegionNodesSpec initNewDescendant(String childCode) {
      return RegionNodesSpec.builder().regionCode(childCode).build();
    }

    @Override
    public List<RegionNodesSpec> getDescendants() {
      return regionNodesSpecs;
    }

    @Override
    public boolean matches(TraversePath traversePath) {
      return true;
    }

    @Override
    public TraversePath updateTraversePath(TraversePath traversePath) {
      return TraversePath.topLevel();
    }

    public RootNodesSpec addRegion(RegionNodesSpec regionNodesSpec) {
      addNewDescendant(regionNodesSpec);
      return this;
    }

    @Override
    public NodesSpec getNodesSpec() {
      return this;
    }

    @Override
    public void validate() {
      validateDescendants();
    }
  }

  /** Region-level node specification, optionally containing per-AZ overrides. */
  @Data
  @SuperBuilder
  @NoArgsConstructor
  @AllArgsConstructor
  public static class RegionNodesSpec extends NodesSpec
      implements WithDescendants<AzNodesSpec>, HasCode {
    @ApiModelProperty private String regionCode;
    @Builder.Default @ApiModelProperty private List<AzNodesSpec> azNodesSpecs = null;

    @Override
    public List<AzNodesSpec> getDescendants() {
      return azNodesSpecs;
    }

    @Override
    public String getCode() {
      return regionCode;
    }

    @Override
    public void initDescendants() {
      if (azNodesSpecs == null) {
        azNodesSpecs = new ArrayList<>();
      }
    }

    @Override
    public AzNodesSpec initNewDescendant(String childCode) {
      return AzNodesSpec.builder().azCode(childCode).build();
    }

    @Override
    public boolean matches(TraversePath traversePath) {
      return Objects.equals(regionCode, traversePath.regionCode);
    }

    @Override
    public TraversePath updateTraversePath(TraversePath traversePath) {
      return TraversePath.regionLevel(regionCode);
    }

    public RegionNodesSpec addZone(AzNodesSpec azNodesSpec) {
      addNewDescendant(azNodesSpec);
      return this;
    }

    @Override
    public NodesSpec getNodesSpec() {
      return this;
    }

    @Override
    public void validate() {
      validateDescendants();
    }
  }

  /** Availability-zone-level node specification (leaf of the hierarchy). */
  @Data
  @SuperBuilder
  @NoArgsConstructor
  @AllArgsConstructor
  public static class AzNodesSpec extends NodesSpec implements HiararchicalNode, HasCode {
    @ApiModelProperty private String azCode;

    @Override
    public String getCode() {
      return azCode;
    }

    @Override
    public boolean matches(TraversePath traversePath) {
      return Objects.equals(azCode, traversePath.azCode);
    }

    @Override
    public TraversePath updateTraversePath(TraversePath traversePath) {
      return TraversePath.azLevel(traversePath.regionCode, azCode);
    }

    @Override
    public NodesSpec getNodesSpec() {
      return this;
    }
  }

  /** Region and AZ codes identifying a position in the specification hierarchy. */
  @Data
  public static class TraversePath {
    private final String regionCode;
    private final String azCode;

    /** Returns a path representing the root (universe-wide) level. */
    public static TraversePath topLevel() {
      return new TraversePath(null, null);
    }

    /**
     * Returns a path scoped to a region.
     *
     * @param regionCode region code
     */
    public static TraversePath regionLevel(String regionCode) {
      return new TraversePath(regionCode, null);
    }

    /**
     * Returns a path scoped to an availability zone.
     *
     * @param regionCode region code containing the AZ
     * @param azCode availability zone code
     */
    public static TraversePath azLevel(String regionCode, String azCode) {
      return new TraversePath(regionCode, azCode);
    }

    private TraversePath(String regionCode, String azCode) {
      this.regionCode = regionCode;
      this.azCode = azCode;
    }

    public TraversePath clone() {
      return new TraversePath(regionCode, azCode);
    }
  }

  /** Resolved node specification together with the path and server type used to resolve it. */
  @Data
  public static class NodeSpecInfo {
    private NodeSpec nodeSpec;
    private TraversePath traversePath;
    @Nullable private UniverseTaskBase.ServerType serverType;
  }

  /** Pair of current and source node specs passed to hierarchy merge callbacks. */
  @Data
  @AllArgsConstructor
  public static class NodesSpecsMergeItem {
    private final @NotNull NodeSpec current;
    private final @NotNull NodeSpec source;
    private final UniverseTaskBase.ServerType serverType;
    private final TraversePath traversePath;
  }

  /**
   * Resolves the effective node spec for the given server type and AZ.
   *
   * <p>Looks up the AZ by UUID to determine region and AZ codes, then merges specs from root
   * through region down to that AZ.
   *
   * @param rootNodesSpec root of the hierarchy
   * @param serverType tserver or master; defaults to tserver when {@code null}
   * @param azUUID AZ to resolve for, or {@code null} for root-level defaults
   * @return merged spec and metadata about how it was resolved
   */
  public static NodeSpecInfo getSpecification(
      RootNodesSpec rootNodesSpec,
      @Nullable UniverseTaskBase.ServerType serverType,
      @Nullable UUID azUUID) {
    TraversePath traversePath;
    if (azUUID != null) {
      AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUUID);
      traversePath = TraversePath.azLevel(az.getRegion().getCode(), az.getCode());
    } else {
      traversePath = TraversePath.topLevel();
    }
    return getSpecification(rootNodesSpec, serverType, traversePath);
  }

  /**
   * Resolves the effective node spec for the given server type and hierarchy path.
   *
   * <p>Merges specs from root through each matching region and AZ on {@code traversePath}.
   *
   * @param rootNodesSpec root of the hierarchy
   * @param serverType tserver or master; defaults to tserver when {@code null}
   * @param traversePath region and/or AZ context to resolve
   * @return merged spec and metadata about how it was resolved
   */
  public static NodeSpecInfo getSpecification(
      RootNodesSpec rootNodesSpec,
      @Nullable UniverseTaskBase.ServerType serverType,
      TraversePath traversePath) {
    NodeSpecInfo result = new NodeSpecInfo();
    result.setServerType(serverType);
    result.setTraversePath(traversePath);

    List<NodesSpec> specs = new ArrayList<>();
    search(rootNodesSpec, specs, traversePath);
    for (NodesSpec nodesSpec : specs) {
      // Not initializing with empty value, just returning empty copy.
      NodeSpec cur = emptyIfNull(getNodeSpec(nodesSpec, serverType, false));
      if (cur == null) {
        cur = NodeSpec.empty();
      }
      if (result.getNodeSpec() == null) {
        result.setNodeSpec(cur.clone());
      } else {
        cur.mergeInto(result.getNodeSpec());
      }
    }
    result.setNodeSpec(emptyIfNull(result.getNodeSpec()));
    return result;
  }

  @NotNull
  private static NodeSpec getNodeSpec(
      NodesSpec nodesSpec, UniverseTaskBase.ServerType serverType, boolean initIfNull) {
    if (nodesSpec == null) {
      return NodeSpec.empty();
    }
    if (serverType == null || serverType == UniverseTaskBase.ServerType.TSERVER) {
      return initIfNull ? nodesSpec.getOrCreateTserverSpec() : nodesSpec.getTserverSpecification();
    }
    if (serverType == UniverseTaskBase.ServerType.MASTER) {
      return initIfNull ? nodesSpec.getOrCreateMasterSpec() : nodesSpec.getMasterSpecification();
    }
    throw new IllegalArgumentException("Server type " + serverType + " is not supported!");
  }

  private static void search(
      HiararchicalNode hierarchicalNode, List<NodesSpec> specs, TraversePath requiredContext) {
    if (hierarchicalNode != null && hierarchicalNode.matches(requiredContext)) {
      specs.add(hierarchicalNode.getNodesSpec());
      if (hierarchicalNode instanceof WithDescendants<? extends HiararchicalNode>) {
        List<? extends HiararchicalNode> descendants =
            ((WithDescendants<? extends HiararchicalNode>) hierarchicalNode).getDescendants();
        if (descendants != null) {
          descendants.stream()
              .filter(d -> d.matches(requiredContext))
              .findFirst()
              .ifPresent(
                  d -> {
                    specs.add(d.getNodesSpec());
                    search(d, specs, requiredContext);
                  });
        }
      }
    }
  }

  /**
   * Invokes {@code callback} for each AZ-level spec defined under {@code rootNodesSpec}.
   *
   * @param providerUUID provider used to resolve AZ codes to UUIDs
   * @param rootNodesSpec root of the hierarchy
   * @param callback receives each AZ UUID and its node spec
   */
  public static void traverseAZSpecs(
      UUID providerUUID, RootNodesSpec rootNodesSpec, BiConsumer<UUID, NodesSpec> callback) {
    Provider provider = Provider.getOrBadRequest(providerUUID);
    if (rootNodesSpec.getRegionNodesSpecs() != null) {
      for (RegionNodesSpec regionNodesSpec : rootNodesSpec.getRegionNodesSpecs()) {
        if (regionNodesSpec.getAzNodesSpecs() != null) {
          Region region = Region.getByCode(provider, regionNodesSpec.getRegionCode());
          for (AzNodesSpec azNodesSpec : regionNodesSpec.getAzNodesSpecs()) {
            AvailabilityZone zone = AvailabilityZone.getByCode(provider, azNodesSpec.getAzCode());
            callback.accept(zone.getUuid(), azNodesSpec.getNodesSpec());
          }
        }
      }
    }
  }

  /**
   * Walks the hierarchy of {@code targetNodeSpec} and {@code sourceNodesSpec} in parallel, invoking
   * {@code consumer} for each pair of tserver/master specs at every level.
   *
   * <p>Missing descendants on either side are created as needed. After merging, empty descendants
   * are pruned from the target.
   *
   * @param targetNodeSpec spec to update in place
   * @param sourceNodesSpec spec to merge from
   * @param consumer merge logic applied per hierarchy node and server type
   */
  public static void merge(
      RootNodesSpec targetNodeSpec,
      RootNodesSpec sourceNodesSpec,
      Consumer<NodesSpecsMergeItem> consumer) {
    if (targetNodeSpec == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Target specification is null");
    }
    if (sourceNodesSpec == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Source specification is null");
    }
    merge(new TraversePath(null, null), targetNodeSpec, sourceNodesSpec, consumer);
  }

  private static void merge(
      TraversePath currentPath,
      @NotNull HiararchicalNode current,
      @NotNull HiararchicalNode source,
      Consumer<NodesSpecsMergeItem> consumer) {
    TraversePath newPath = current.updateTraversePath(currentPath);
    NodesSpec currentNodesSpec = current.getNodesSpec();
    NodesSpec sourceNodesSpec = source.getNodesSpec();
    for (UniverseTaskBase.ServerType serverType :
        Arrays.asList(UniverseTaskBase.ServerType.TSERVER, UniverseTaskBase.ServerType.MASTER)) {

      NodeSpec currentSpec = getNodeSpec(currentNodesSpec, serverType, true);
      NodeSpec sourceSpec = emptyIfNull(getNodeSpec(sourceNodesSpec, serverType, false));

      if (!currentSpec.isEmpty() || !sourceSpec.isEmpty()) {
        NodesSpecsMergeItem nodesSpecsMergeItem =
            new NodesSpecsMergeItem(currentSpec, sourceSpec, serverType, newPath);
        consumer.accept(nodesSpecsMergeItem);
      }
    }
    if (current instanceof WithDescendants<? extends HiararchicalNode>) {
      mergeDescendants(
          newPath,
          (WithDescendants<? extends HiararchicalNode>) current,
          (WithDescendants<? extends HiararchicalNode>) source,
          consumer);

      ((WithDescendants<? extends HiararchicalNode>) current).clearEmptyDescendants();
    }
    log.debug("Clean empty specs for " + current);
    current.clean();
  }

  private static void mergeDescendants(
      TraversePath context,
      @NotNull WithDescendants<? extends HiararchicalNode> current,
      @NotNull WithDescendants<? extends HiararchicalNode> source,
      Consumer<NodesSpecsMergeItem> consumer) {
    Set<String> allKeys = new HashSet<>();
    allKeys.addAll(getDescendantKeys(current));
    allKeys.addAll(getDescendantKeys(source));
    for (String key : allKeys) {
      HiararchicalNode curDescendant = current.getOrCreateDescendant(key);
      HiararchicalNode sourceDescendant = source.getOrCreateDescendant(key);
      merge(context, curDescendant, sourceDescendant, consumer);
      curDescendant.clean();
    }
  }

  private static Set<String> getDescendantKeys(WithDescendants<? extends HiararchicalNode> node) {
    Set<String> result = new HashSet<>();
    if (node.getDescendants() == null) {
      return result;
    }
    for (HiararchicalNode descendant : node.getDescendants()) {
      if (descendant instanceof HasCode) {
        String code = ((HasCode) descendant).getCode();
        if (code != null) {
          result.add(code);
        }
      }
    }
    return result;
  }

  private static NodeSpec emptyIfNull(NodeSpec spec) {
    return spec == null ? NodeSpec.empty() : spec;
  }
}
