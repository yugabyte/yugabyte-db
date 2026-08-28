// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pa;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuth;
import com.yugabyte.yw.models.helpers.paendpoint.PaEndpointAuthType;
import com.yugabyte.yw.models.helpers.paendpoint.PerfAdvisorEndpointType;
import io.ebean.annotation.Transactional;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Perf Advisor Endpoints: the destinations a universe registered in online mode sends its collected
 * data to.
 *
 * <p>YBA stores these; a collector only learns about one when a universe registered to it needs it.
 * The push is keyed by the endpoint's own uuid, which the collector treats as its export config id.
 */
@Singleton
@Slf4j
public class PerfAdvisorEndpointService {

  private final BeanValidator beanValidator;
  private final PerfAdvisorClient client;
  private final PerfAdvisorService perfAdvisorService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public PerfAdvisorEndpointService(
      BeanValidator beanValidator,
      PerfAdvisorClient client,
      PerfAdvisorService perfAdvisorService,
      RuntimeConfGetter confGetter) {
    this.beanValidator = beanValidator;
    this.client = client;
    this.perfAdvisorService = perfAdvisorService;
    this.confGetter = confGetter;
  }

  /**
   * Online mode is off by default, and the flag gates this entity as well as the registration mode
   * - a configuration surface for a disabled feature would be a feature that is only half off.
   */
  public void checkEnabled(UUID customerUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    if (!confGetter.getConfForScope(customer, CustomerConfKeys.enablePaOnlineMode)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Perf Advisor online mode is not enabled. Set "
              + CustomerConfKeys.enablePaOnlineMode.getKey()
              + " to true to configure Perf Advisor Endpoints.");
    }
  }

  public List<PerfAdvisorEndpoint> list(UUID customerUuid) {
    return PerfAdvisorEndpoint.createQuery().eq("customerUUID", customerUuid).findList();
  }

  public PerfAdvisorEndpoint get(UUID customerUuid, UUID uuid) {
    return PerfAdvisorEndpoint.createQuery()
        .eq("customerUUID", customerUuid)
        .idEq(uuid)
        .findOneOrEmpty()
        .orElse(null);
  }

  public PerfAdvisorEndpoint getOrBadRequest(UUID customerUuid, UUID uuid) {
    PerfAdvisorEndpoint endpoint = get(customerUuid, uuid);
    if (endpoint == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Perf Advisor Endpoint " + uuid + " does not exist");
    }
    return endpoint;
  }

  @Transactional
  public PerfAdvisorEndpoint create(PerfAdvisorEndpoint endpoint) {
    endpoint.generateUUID();
    validate(endpoint);
    Date now = new Date();
    endpoint.setCreateTime(now);
    endpoint.setUpdateTime(now);
    // Validated against the destination before it is stored: an endpoint that cannot be reached is
    // one a later registration can only fail on, and this is where the operator can still fix it.
    validateAgainstDestination(endpoint);
    endpoint.save();
    return endpoint;
  }

  @Transactional
  public PerfAdvisorEndpoint update(PerfAdvisorEndpoint endpoint) {
    validate(endpoint);
    endpoint.setUpdateTime(new Date());
    validateAgainstDestination(endpoint);
    endpoint.update();
    // Collectors already holding this endpoint would otherwise keep the previous credentials until
    // the next sync tick, and a rejection there would surface in a log rather than to the caller.
    pushToCollectorsUsing(endpoint);
    return endpoint;
  }

  @Transactional
  public void delete(UUID customerUuid, UUID uuid) {
    PerfAdvisorEndpoint endpoint = getOrBadRequest(customerUuid, uuid);
    List<Universe> inUse = universesUsing(endpoint);
    if (!inUse.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Perf Advisor Endpoint '"
              + endpoint.getName()
              + "' is still used by universes "
              + inUse.stream().map(Universe::getName).collect(Collectors.joining(", ")));
    }
    // Best effort: with no universe pointing at it no collector should still hold it, and a
    // collector that is down must not block removing a YBA-side record.
    for (PACollector collector : collectors(customerUuid)) {
      try {
        client.deleteExportConfig(collector, endpoint.getUuid());
      } catch (Exception e) {
        log.warn(
            "Failed to remove export config {} from collector {}",
            endpoint.getUuid(),
            collector.getUuid(),
            e);
      }
    }
    endpoint.delete();
  }

  /** The universes registered in online mode against this endpoint. */
  public List<Universe> universesUsing(PerfAdvisorEndpoint endpoint) {
    return Universe.getAllWithoutResources(Customer.get(endpoint.getCustomerUUID())).stream()
        .filter(u -> endpoint.getUuid().equals(u.getUniverseDetails().getPaEndpointUuid()))
        .toList();
  }

  /** The collectors that have at least one universe pointing at this endpoint. */
  public Set<UUID> collectorsUsing(PerfAdvisorEndpoint endpoint) {
    return universesUsing(endpoint).stream()
        .map(u -> u.getUniverseDetails().getPaCollectorUuid())
        .filter(java.util.Objects::nonNull)
        .collect(Collectors.toSet());
  }

  /** The endpoint as the collector's own export config. Identity is shared deliberately. */
  public PerfAdvisorClient.ExportConfig toExportConfig(PerfAdvisorEndpoint endpoint) {
    return new PerfAdvisorClient.ExportConfig()
        .setId(endpoint.getUuid())
        .setName(endpoint.getName())
        .setMetricsEndpoint(endpoint.getMetricsEndpoint())
        .setMetricsType(endpoint.getMetricsType())
        .setMetricsAuth(toExportAuth(endpoint.getMetricsAuth()))
        .setCollectionEndpoint(endpoint.getCollectionEndpoint())
        .setCollectionAuth(toExportAuth(endpoint.getCollectionAuth()))
        .setYbmAccountId(endpoint.getYbmAccountId())
        .setYbmProjectId(endpoint.getYbmProjectId())
        // The destination always receives Perf Advisor's own universe-less metrics along with the
        // per-universe ones: a deployment sending its data to an external Perf Advisor wants the
        // whole picture there. Not offered as a choice, so it is not read from the request either.
        .setIncludeGlobalPaMetrics(true);
  }

  /** Pushes the endpoint to one collector. Used by registration and by the sync loop. */
  public void push(PACollector collector, PerfAdvisorEndpoint endpoint) {
    client.updateExportConfig(collector, toExportConfig(endpoint));
  }

  /**
   * Removes an endpoint's export config from one collector once no universe on that collector needs
   * it. Best effort: the collector refuses while a universe still names it, and that refusal is the
   * backstop rather than the error path.
   */
  public void removeIfUnused(PACollector collector, UUID endpointUuid) {
    boolean stillNeeded =
        Universe.getAllWithoutResources(Customer.get(collector.getCustomerUUID())).stream()
            .anyMatch(
                u ->
                    collector.getUuid().equals(u.getUniverseDetails().getPaCollectorUuid())
                        && endpointUuid.equals(u.getUniverseDetails().getPaEndpointUuid()));
    if (stillNeeded) {
      return;
    }
    try {
      client.deleteExportConfig(collector, endpointUuid);
    } catch (Exception e) {
      log.warn(
          "Failed to remove export config {} from collector {}",
          endpointUuid,
          collector.getUuid(),
          e);
    }
  }

  private void pushToCollectorsUsing(PerfAdvisorEndpoint endpoint) {
    Map<UUID, PACollector> byUuid =
        collectors(endpoint.getCustomerUUID()).stream()
            .collect(Collectors.toMap(PACollector::getUuid, c -> c));
    for (UUID collectorUuid : collectorsUsing(endpoint)) {
      PACollector collector = byUuid.get(collectorUuid);
      if (collector == null) {
        continue;
      }
      push(collector, endpoint);
    }
  }

  private List<PACollector> collectors(UUID customerUuid) {
    return perfAdvisorService.list(PACollectorFilter.builder().customerUuid(customerUuid).build());
  }

  /**
   * Probes both of the endpoint's destinations from a collector, which is the process that will
   * actually send the data - YBA may sit in a different network and does not speak the Collection
   * API. Returns null when the customer has no collector to probe from, which the caller reports as
   * "not contradicted" rather than as a failure.
   */
  public PerfAdvisorClient.ExportConfigValidationReport probe(PerfAdvisorEndpoint endpoint) {
    List<PACollector> collectors = collectors(endpoint.getCustomerUUID());
    if (collectors.isEmpty()) {
      log.warn(
          "No PA Collector for customer {}, cannot probe the destination of endpoint {}",
          endpoint.getCustomerUUID(),
          endpoint.getName());
      return null;
    }
    return client.validateExportConfig(collectors.get(0), toExportConfig(endpoint));
  }

  /** The save path: an endpoint whose destination contradicts it must not be stored. */
  private void validateAgainstDestination(PerfAdvisorEndpoint endpoint) {
    PerfAdvisorClient.ExportConfigValidationReport report = probe(endpoint);
    if (report == null || report.isValid()) {
      return;
    }
    BeanValidator.ErrorMessageBuilder errors = beanValidator.error();
    report.failures().forEach(check -> errors.forField(check.getField(), check.getMessage()));
    errors.throwError();
  }

  private void validate(PerfAdvisorEndpoint endpoint) {
    beanValidator.validate(endpoint);

    if (endpoint.getType() == PerfAdvisorEndpointType.PA_ONLINE) {
      beanValidator
          .error()
          .forField("type", "PA_ONLINE endpoints are not supported yet")
          .throwError();
    }
    if (StringUtils.isEmpty(endpoint.getName())) {
      beanValidator.error().forField("name", "is required").throwError();
    }
    boolean nameTaken =
        list(endpoint.getCustomerUUID()).stream()
            .anyMatch(
                existing ->
                    existing.getName().equals(endpoint.getName())
                        && !existing.getUuid().equals(endpoint.getUuid()));
    if (nameTaken) {
      beanValidator
          .error()
          .forField("name", "endpoint with name '" + endpoint.getName() + "' already exists")
          .throwError();
    }
    validateAuth(endpoint.getMetricsAuth(), "metricsAuth");
    validateAuth(endpoint.getCollectionAuth(), "collectionAuth");
  }

  private void validateAuth(PaEndpointAuth auth, String field) {
    if (auth == null || auth.getType() != PaEndpointAuthType.BASIC) {
      return;
    }
    if (StringUtils.isEmpty(auth.getUsername())) {
      beanValidator
          .error()
          .forField(field + ".username", "is required for basic authentication")
          .throwError();
    }
  }

  private static PerfAdvisorClient.ExportAuth toExportAuth(PaEndpointAuth auth) {
    if (auth == null) {
      return null;
    }
    return new PerfAdvisorClient.ExportAuth()
        .setType(
            auth.getType() == PaEndpointAuthType.BASIC
                ? PerfAdvisorClient.ExportAuthType.basic
                : PerfAdvisorClient.ExportAuthType.none)
        .setUsername(auth.getUsername())
        .setPassword(auth.getPassword());
  }
}
