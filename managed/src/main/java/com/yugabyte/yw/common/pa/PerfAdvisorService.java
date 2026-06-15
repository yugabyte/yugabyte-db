/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.pa;

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.forms.PaUniverseInfo;
import com.yugabyte.yw.forms.PaUniverseInfo.SortBy;
import com.yugabyte.yw.forms.paging.PaUniverseApiFilter;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiQuery;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiResponse;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.ExpressionList;
import io.ebean.annotation.Transactional;
import java.util.*;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class PerfAdvisorService {
  private final BeanValidator beanValidator;
  private final PerfAdvisorClient client;
  private final RuntimeConfGetter confGetter;
  private final SettableRuntimeConfigFactory configFactory;
  private final MetricQueryHelper metricQueryHelper;

  @Inject
  public PerfAdvisorService(
      BeanValidator beanValidator,
      PerfAdvisorClient client,
      RuntimeConfGetter confGetter,
      SettableRuntimeConfigFactory configFactory,
      MetricQueryHelper metricQueryHelper) {
    this.beanValidator = beanValidator;
    this.client = client;
    this.confGetter = confGetter;
    this.configFactory = configFactory;
    this.metricQueryHelper = metricQueryHelper;
  }

  @Transactional
  public PACollector save(PACollector paCollector, boolean force) {
    boolean isUpdate = false;
    if (paCollector.getUuid() == null) {
      paCollector.generateUUID();
    } else {
      isUpdate = true;
      PACollector existingConfig =
          getOrBadRequest(paCollector.getCustomerUUID(), paCollector.getUuid());
      if (!force && !existingConfig.getPaUrl().equals(paCollector.getPaUrl())) {
        PACollectorExt.InUseStatus inUseStatus = client.getInUseStatus(existingConfig);
        if (inUseStatus == PACollectorExt.InUseStatus.IN_USE) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't change PA Collector URL while collector is in use");
        } else if (inUseStatus == PACollectorExt.InUseStatus.ERROR) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Can't change PA Collector URL while old URL is not accessible");
        }
      }
    }

    validate(paCollector);
    paCollector.setPaUrl(normalizeUrl(paCollector.getPaUrl()));
    paCollector.setMetricsUrl(normalizeUrl(paCollector.getMetricsUrl()));
    paCollector.setYbaUrl(normalizeUrl(paCollector.getYbaUrl()));
    client.putCustomerMetadata(paCollector);
    if (isUpdate) {
      paCollector.update();
    } else {
      paCollector.save();
    }
    return paCollector;
  }

  public PACollector create(PACollector paCollector) {
    validate(paCollector);
    paCollector.setPaUrl(normalizeUrl(paCollector.getPaUrl()));
    paCollector.setMetricsUrl(normalizeUrl(paCollector.getMetricsUrl()));
    paCollector.setYbaUrl(normalizeUrl(paCollector.getYbaUrl()));
    client.putCustomerMetadata(paCollector);
    paCollector.save();
    return paCollector;
  }

  public PACollector get(UUID customerUuid, UUID uuid) {
    if (uuid == null || customerUuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get PA Collector by null uuid");
    }
    PACollectorFilter filter =
        PACollectorFilter.builder().customerUuid(customerUuid).uuid(uuid).build();
    return list(filter).stream().findFirst().orElse(null);
  }

  public PACollector getOrBadRequest(UUID customerUUID, UUID uuid) {
    PACollector platform = get(customerUUID, uuid);
    if (platform == null) {
      throw new PlatformServiceException(BAD_REQUEST, "PA Collector not found");
    }
    return platform;
  }

  public List<PACollector> list(PACollectorFilter filter) {
    ExpressionList<PACollector> query = PACollector.createQuery();
    if (CollectionUtils.isNotEmpty(filter.getUuids())) {
      query = appendInClause(PACollector.createQuery(), "uuid", filter.getUuids());
    }
    if (filter.getCustomerUuid() != null) {
      query = query.eq("customerUUID", filter.getCustomerUuid());
    }
    if (filter.getPaUrl() != null) {
      query = query.eq("paUrl", filter.getPaUrl());
    }
    return query.findList();
  }

  @Transactional
  public void delete(UUID customerUuid, UUID uuid, boolean force) {
    PACollector platform = getOrBadRequest(customerUuid, uuid);
    try {
      client.deleteCustomerMetadata(platform);
    } catch (PlatformServiceException e) {
      if (!force) {
        throw e;
      }
    }
    platform.delete();
  }

  public PACollectorExt.InUseStatus getInUseStatus(PACollector platform) {
    return client.getInUseStatus(platform);
  }

  public boolean isRegistered(PACollector paCollector, Universe universe) {
    return client.getUniverseMetadata(paCollector, universe.getUniverseUUID()) != null;
  }

  public PerfAdvisorClient.UniverseMetadata getUniverseMetadata(
      PACollector paCollector, Universe universe) {
    return client.getUniverseMetadata(paCollector, universe.getUniverseUUID());
  }

  public List<PerfAdvisorClient.UniverseMetadata> listRegisteredUniverses(PACollector collector) {
    return client.listUniverseMetadata(collector);
  }

  public PaUniversePagedApiResponse pagedListRegisteredUniverses(
      PACollector collector, PaUniversePagedApiQuery apiQuery) {
    List<PerfAdvisorClient.UniverseMetadata> allMetadata = listRegisteredUniverses(collector);

    Stream<PaUniverseInfo> infoStream =
        allMetadata.stream()
            .map(
                meta -> {
                  PaUniverseInfo info = new PaUniverseInfo();
                  info.setUniverseUuid(meta.getId());
                  Optional<Universe> universe = Universe.maybeGet(meta.getId());
                  info.setUniverseName(universe.map(Universe::getName).orElse(null));
                  info.setDataMountPoints(meta.getDataMountPoints());
                  info.setOtherMountPoints(meta.getOtherMountPoints());
                  info.setAdvancedObservability(meta.isMetricsExportToPrometheusEnabled());
                  return info;
                });

    PaUniverseApiFilter filter = apiQuery.getFilter();
    if (filter != null && StringUtils.isNotEmpty(filter.getUniverseName())) {
      String nameFilter = filter.getUniverseName().toLowerCase();
      infoStream =
          infoStream.filter(
              i ->
                  i.getUniverseName() != null
                      && i.getUniverseName().toLowerCase().contains(nameFilter));
    }

    Comparator<PaUniverseInfo> comparator =
        Comparator.comparing(
            i -> {
              if (apiQuery.getSortBy() == SortBy.universeUuid) {
                return i.getUniverseUuid().toString();
              }
              return i.getUniverseName();
            });
    if (apiQuery.getDirection() == SortDirection.DESC) {
      comparator = comparator.reversed();
    }

    List<PaUniverseInfo> sorted = infoStream.sorted(comparator).toList();

    int totalCount = sorted.size();
    int offset = apiQuery.getOffset();
    int limit = apiQuery.getLimit();
    int fromIndex = Math.min(offset, totalCount);
    int toIndex = Math.min(offset + limit, totalCount);

    PaUniversePagedApiResponse response = new PaUniversePagedApiResponse();
    response.setEntities(sorted.subList(fromIndex, toIndex));
    response.setHasPrev(offset > 0);
    response.setHasNext(toIndex < totalCount);
    response.setTotalCount(totalCount);
    return response;
  }

  /**
   * Per-universe Performance Advisor memory consumption mode used by the memory precheck. The
   * caller passes the current and target modes; the validator looks up the per-node memory budget
   * for each from runtime config and checks that the YBA node has enough headroom for the delta.
   */
  public enum PaMemoryMode {
    /** No PA collection. */
    NONE,
    /** PA collector enabled without advanced observability. */
    COLLECTOR_ONLY,
    /** PA collector enabled with advanced observability. */
    ADVANCED,
  }

  /**
   * Validates that the YBA installation has enough free memory to transition the universe from
   * {@code currentMode} to {@code targetMode}. The PA-collector budget is consumed in the yugaware
   * container; the advanced-observability budget is consumed in the prometheus container. In K8s we
   * validate each container's headroom independently; on VM both run on the same host so we
   * validate the sum against the host's available memory. When the target is the same as or below
   * the current mode (e.g. disabling advanced observability or unregistering) the change frees
   * memory, so this is a no-op.
   *
   * @param actionDescription short human-readable description of the action that triggered the
   *     validation (for example {@code "Cannot register universe with Performance Advisor"}). It is
   *     prefixed onto any BAD_REQUEST message produced by this method so the user can tell which
   *     operation was rejected.
   */
  public void validatePerfAdvisorMemory(
      Universe universe,
      PaMemoryMode currentMode,
      PaMemoryMode targetMode,
      String actionDescription) {
    if (confGetter.getGlobalConf(GlobalConfKeys.skipPaMemoryValidation)) {
      return;
    }

    int additionalCollectorPerNodeMb =
        Math.max(0, yugawareMemoryPerNodeMb(targetMode) - yugawareMemoryPerNodeMb(currentMode));
    int additionalAdvancedPerNodeMb =
        Math.max(0, prometheusMemoryPerNodeMb(targetMode) - prometheusMemoryPerNodeMb(currentMode));
    if (additionalCollectorPerNodeMb == 0 && additionalAdvancedPerNodeMb == 0) {
      return;
    }

    // Count tservers from nodeDetailsSet directly. Universe.getTServers() filters out nodes
    // whose cloud private_ip is not set yet, which is the case while CreateUniverse runs its
    // prechecks - we'd otherwise see 0 tservers and silently skip the validation.
    int tserverCount =
        (int)
            universe.getUniverseDetails().nodeDetailsSet.stream().filter(n -> n.isTserver).count();
    long additionalCollectorMb = (long) tserverCount * additionalCollectorPerNodeMb;
    long additionalAdvancedMb = (long) tserverCount * additionalAdvancedPerNodeMb;

    if (KubernetesEnvironmentVariables.isYbaRunningInKubernetes()) {
      validateK8sContainerMemory(
          "yugaware",
          additionalCollectorMb,
          tserverCount,
          additionalCollectorPerNodeMb,
          actionDescription);
      validateK8sContainerMemory(
          "prometheus",
          additionalAdvancedMb,
          tserverCount,
          additionalAdvancedPerNodeMb,
          actionDescription);
      return;
    }

    long requiredAdditionalMb = additionalCollectorMb + additionalAdvancedMb;
    long availableMemoryMb = getVmAvailableMemoryMb();
    if (availableMemoryMb < 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not determine YBA node available memory");
    }
    if (requiredAdditionalMb > availableMemoryMb) {
      int additionalPerNodeMb = additionalCollectorPerNodeMb + additionalAdvancedPerNodeMb;
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s: this change would require approximately %d MB of additional memory "
                  + "(%d tserver nodes x %d MB/node), but only %d MB is available on the YBA node.",
              actionDescription,
              requiredAdditionalMb,
              tserverCount,
              additionalPerNodeMb,
              availableMemoryMb));
    }
  }

  private void validateK8sContainerMemory(
      String containerName,
      long requiredMb,
      int tserverCount,
      int perNodeMb,
      String actionDescription) {
    if (requiredMb == 0) {
      return;
    }
    long availableMb = getK8sContainerAvailableMemoryMb(containerName);
    if (availableMb < 0) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Could not determine available memory for container %s", containerName));
    }
    if (requiredMb > availableMb) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "%s: this change would require approximately %d MB of additional memory in"
                  + " container %s (%d tserver nodes x %d MB/node), but only %d MB is available.",
              actionDescription, requiredMb, containerName, tserverCount, perNodeMb, availableMb));
    }
  }

  /** Memory consumed by Performance Advisor data ingestion in the yugaware container, per node. */
  private int yugawareMemoryPerNodeMb(PaMemoryMode mode) {
    switch (mode) {
      case NONE:
        return 0;
      case COLLECTOR_ONLY:
      case ADVANCED:
        return confGetter.getGlobalConf(GlobalConfKeys.paMemoryPerNodePaCollectorMb);
    }
    throw new IllegalArgumentException("Unknown PaMemoryMode: " + mode);
  }

  /**
   * Additional memory consumed in the prometheus container when advanced observability is enabled,
   * per node. This is the increment on top of the yugaware container budget, so the configured
   * advanced-observability total is reduced by the PA collector budget.
   */
  private int prometheusMemoryPerNodeMb(PaMemoryMode mode) {
    switch (mode) {
      case NONE:
      case COLLECTOR_ONLY:
        return 0;
      case ADVANCED:
        int totalMb =
            confGetter.getGlobalConf(GlobalConfKeys.paMemoryPerNodeAdvancedObservabilityMb);
        int collectorMb = confGetter.getGlobalConf(GlobalConfKeys.paMemoryPerNodePaCollectorMb);
        return Math.max(0, totalMb - collectorMb);
    }
    throw new IllegalArgumentException("Unknown PaMemoryMode: " + mode);
  }

  private long getVmAvailableMemoryMb() {
    // Linux node_exporter exposes node_memory_MemAvailable_bytes directly; macOS does not, so
    // we fall back to free + inactive + purgeable (memory the kernel can reclaim without
    // significant impact). The `or` guarantees we pick whichever series the YBA host publishes.
    String query =
        "node_memory_MemAvailable_bytes{job=\"yba-node-exporter\"}"
            + " or ("
            + "node_memory_free_bytes{job=\"yba-node-exporter\"}"
            + " + node_memory_inactive_bytes{job=\"yba-node-exporter\"}"
            + " + node_memory_purgeable_bytes{job=\"yba-node-exporter\"})";
    try {
      ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect(query);
      if (results != null && !results.isEmpty()) {
        double bytes = results.get(0).values.get(0).getRight();
        return (long) (bytes / (1024 * 1024));
      }
    } catch (Exception e) {
      log.warn("Failed to query YBA node available memory from Prometheus", e);
    }
    return -1;
  }

  private long getK8sContainerAvailableMemoryMb(String containerName) {
    String podName = KubernetesEnvironmentVariables.getPodName();
    String namespace = KubernetesEnvironmentVariables.getPodNamespace();
    if (podName == null || namespace == null) {
      return -1;
    }
    try {
      // Prefer the configured limit (kube-state-metrics doesn't emit the series when the limit
      // is unset). If the limit is missing or 0, fall back to the configured request - same
      // semantics as the K8s alert templates.
      long capacityBytes =
          queryScalarBytes(
              String.format(
                  "max(kube_pod_container_resource_limits_memory_bytes{pod_name=\"%s\","
                      + "container_name=\"%s\",namespace=\"%s\"})",
                  podName, containerName, namespace));
      if (capacityBytes <= 0) {
        capacityBytes =
            queryScalarBytes(
                String.format(
                    "max(kube_pod_container_resource_requests_memory_bytes{pod_name=\"%s\","
                        + "container_name=\"%s\",namespace=\"%s\"})",
                    podName, containerName, namespace));
      }
      if (capacityBytes <= 0) {
        return -1;
      }
      long usedBytes =
          queryScalarBytes(
              String.format(
                  "max(container_memory_working_set_bytes{pod_name=\"%s\","
                      + "container_name=\"%s\",namespace=\"%s\"})",
                  podName, containerName, namespace));
      if (usedBytes < 0) {
        return -1;
      }
      long availableBytes = Math.max(0L, capacityBytes - usedBytes);
      return availableBytes / (1024 * 1024);
    } catch (Exception e) {
      log.warn("Failed to query container memory for {} from Prometheus", containerName, e);
    }
    return -1;
  }

  /**
   * Runs an instant Prometheus query that is expected to produce a single scalar/series value in
   * bytes and returns it. Returns -1 when the series is missing or the query fails.
   */
  private long queryScalarBytes(String query) {
    ArrayList<MetricQueryResponse.Entry> results = metricQueryHelper.queryDirect(query);
    if (results == null || results.isEmpty()) {
      return -1;
    }
    return (long) (double) results.get(0).values.get(0).getRight();
  }

  public void putUniverse(
      PACollector paCollector, Universe universe, boolean advancedObservability) {
    RuntimeConfig<Universe> runtimeConfig = configFactory.forUniverse(universe);

    boolean dbQueryApiEnabled =
        runtimeConfig.getBoolean(UniverseConfKeys.enableDbQueryApi.getKey());
    if (!dbQueryApiEnabled) {
      log.info(
          "Enabling {} for universe {}",
          UniverseConfKeys.enableDbQueryApi.getKey(),
          universe.getUniverseUUID());
      runtimeConfig.setValue(UniverseConfKeys.enableDbQueryApi.getKey(), Boolean.TRUE.toString());
    }

    PerfAdvisorClient.UniverseMetadata universeMetadata =
        new PerfAdvisorClient.UniverseMetadata()
            .setId(universe.getUniverseUUID())
            .setCustomerId(paCollector.getCustomerUUID())
            .setDataMountPoints(splitMountPoints(MetricQueryHelper.getDataMountPoints(universe)))
            .setOtherMountPoints(
                splitMountPoints(MetricQueryHelper.getOtherMountPoints(confGetter, universe)))
            .setMetricsExportToPrometheusEnabled(advancedObservability);
    client.putUniverseMetadata(paCollector, universeMetadata);
  }

  public void deleteUniverse(PACollector paCollector, Universe universe) {
    client.deleteUniverseMetadata(paCollector, universe.getUniverseUUID());
  }

  private List<String> splitMountPoints(String mountPoints) {
    return Arrays.stream(mountPoints.split("\\|")).toList();
  }

  public void validate(PACollector platform) {
    beanValidator.validate(platform);

    PACollectorFilter filter =
        PACollectorFilter.builder()
            .customerUuid(platform.getCustomerUUID())
            .paUrl(platform.getPaUrl())
            .build();
    List<PACollector> platformWithSameUrl = list(filter);
    if (CollectionUtils.isNotEmpty(platformWithSameUrl)
        && !platformWithSameUrl.get(0).getUuid().equals(platform.getUuid())) {
      beanValidator
          .error()
          .forField("paUrl", "collector with such url already exists.")
          .throwError();
    }
  }

  private String normalizeUrl(String url) {
    if (url.endsWith("/")) {
      return url.substring(0, url.length() - 1);
    }
    return url;
  }
}
