package com.yugabyte.yw.common.pa;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.time.Duration;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class PACollectorSync {

  private final SettableRuntimeConfigFactory configFactory;
  private final PerfAdvisorService perfAdvisorService;
  private final MetricUrlProvider metricUrlProvider;
  private final RoleBindingUtil roleBindingUtil;
  private final PlatformScheduler platformScheduler;
  private final PerfAdvisorEndpointService endpointService;
  private final RuntimeConfGetter confGetter;

  /**
   * What was last pushed to which collector, as the endpoint's {@code updateTime}. The collector
   * returns passwords masked, so comparing its copy field by field cannot tell a changed credential
   * from an unchanged one - and every push makes the collector dial the destination, so re-pushing
   * on every tick is not an option either. An empty map after a restart is what produces the one
   * full pass on boot.
   */
  private final Map<CollectorEndpoint, Date> lastSynced = new ConcurrentHashMap<>();

  /**
   * Collectors whose export configs have been reconciled since this process started. The boot pass
   * is what repairs drift left by a restore, a hand edit on the collector, or a push that failed
   * while YBA was down; afterwards removals are handled where they happen, on unregister.
   */
  private final Set<UUID> reconciledCollectors = ConcurrentHashMap.newKeySet();

  private record CollectorEndpoint(UUID collectorUuid, UUID endpointUuid) {}

  private final MetricService metricService;

  @Inject
  public PACollectorSync(
      SettableRuntimeConfigFactory configFactory,
      PerfAdvisorService perfAdvisorService,
      MetricUrlProvider metricUrlProvider,
      RoleBindingUtil roleBindingUtil,
      PlatformScheduler platformScheduler,
      MetricService metricService,
      PerfAdvisorEndpointService endpointService,
      RuntimeConfGetter confGetter) {
    this.configFactory = configFactory;
    this.perfAdvisorService = perfAdvisorService;
    this.metricUrlProvider = metricUrlProvider;
    this.roleBindingUtil = roleBindingUtil;
    this.platformScheduler = platformScheduler;
    this.metricService = metricService;
    this.endpointService = endpointService;
    this.confGetter = confGetter;
  }

  public void start() {
    log.info("Started PA Collector sync");
    // Recurring, and runs on followers too. This is what pushes local PA URLs and
    // collection_enabled=false to the embedded PA's customer_metadata; if it only ran on the
    // leader the standby PA would never get its state refreshed after an HA restore, and the
    // collection_enabled gate would never flip after a role change that doesn't restart YBA
    // (yb.ha.shutdown_level=0). Export config convergence is leader-only - see syncExportConfigs.
    platformScheduler.scheduleAlwaysOn(
        getClass().getSimpleName(),
        Duration.ZERO, // InitialDelay
        Duration.ofMinutes(1), // Interval
        this::initializeAll);
  }

  private void initializeAll() {
    initializeInternal(Customer.getAll());
  }

  public void initialize(Customer customer) {
    initializeInternal(ImmutableList.of(customer));
  }

  private void initializeInternal(List<Customer> customers) {
    if (customers.isEmpty()) {
      return;
    }
    String embeddedPaUrl = configFactory.staticApplicationConf().getString("yb.pa.url");
    String embeddedPaToken = configFactory.staticApplicationConf().getString("yb.pa.api_token");
    String platformUrl = configFactory.staticApplicationConf().getString("yb.platform.url");
    long scrapeInterval =
        SwamperHelper.getScrapeIntervalSeconds(configFactory.staticApplicationConf());
    String prometheusUrl = metricUrlProvider.getMetricsInternalUrl();

    if (StringUtils.isNotEmpty(embeddedPaUrl)) {
      boolean paCollectorEnabled =
          configFactory.globalRuntimeConf().getBoolean(CustomerConfKeys.enablePACollector.getKey());
      if (!paCollectorEnabled) {
        log.info("Enabling {}", CustomerConfKeys.enablePACollector.getKey());
        configFactory
            .globalRuntimeConf()
            .setValue(CustomerConfKeys.enablePACollector.getKey(), Boolean.TRUE.toString());
      }
    }

    String metricsUsername =
        configFactory
            .staticApplicationConf()
            .getString(GlobalConfKeys.metricsAuthUsername.getKey());
    String metricsPassword =
        configFactory
            .staticApplicationConf()
            .getString(GlobalConfKeys.metricsAuthPassword.getKey());
    // Uses isLocalLeader() (not the switchover-aware HighAvailabilityConfig.isFollower())
    // so the post-promotion initialize() call from PlatformReplicationManager, which still
    // runs while isSwitchOverInProgress=true, sees the newly-promoted instance as a leader
    // and takes the leader path.
    boolean isFollower = HighAvailabilityConfig.get().map(c -> !c.isLocalLeader()).orElse(false);
    for (Customer customer : customers) {
      try {
        // Ahead of the embedded lifecycle below, which has paths that skip to the next customer.
        // On a very first boot there is no collector yet and this does nothing; the next tick
        // picks it up.
        syncExportConfigs(customer, isFollower);

        // Find the embedded collector using the explicit marker. After an HA restore the
        // paUrl on the row still points at the old active's PA, so URL-based lookup would
        // miss it; the embedded flag is the stable identity.
        List<PACollector> embeddedMatches =
            perfAdvisorService.list(
                PACollectorFilter.builder()
                    .customerUuid(customer.getUuid())
                    .embedded(true)
                    .build());
        PACollector embeddedCollector = embeddedMatches.isEmpty() ? null : embeddedMatches.get(0);

        // On the follower we only need to mark our local collector as hot standby.
        // All other cases we skip.
        if (isFollower && (embeddedCollector == null || StringUtils.isEmpty(embeddedPaUrl))) {
          continue;
        }

        if (StringUtils.isEmpty(embeddedPaUrl) && embeddedCollector == null) {
          continue;
        } else if (StringUtils.isNotEmpty(embeddedPaUrl) && embeddedCollector == null) {
          String paUserEmail = "pa_collector_" + customer.getId() + "@yugabyte.com";
          Users paUser = Users.getByEmail(paUserEmail);
          if (paUser == null) {
            log.info("Creating PA collector service account {}", paUserEmail);
            paUser =
                Users.create(
                    paUserEmail,
                    Util.getRandomPassword(),
                    Users.Role.Admin,
                    customer.getUuid(),
                    false);

            Role adminRole = Role.get(customer.getUuid(), Users.Role.Admin.name());
            ResourceGroup resourceGroup =
                ResourceGroup.getSystemDefaultResourceGroup(customer.getUuid(), paUser);
            roleBindingUtil.createRoleBinding(
                paUser.getUuid(), adminRole.getRoleUUID(), RoleBindingType.System, resourceGroup);
          }
          String apiToken = paUser.upsertApiToken();

          log.info("Adding embedded collector for customer {}", customer.getUuid());
          PACollector collector = new PACollector();
          collector.setUuid(customer.getUuid());
          collector.setPaApiToken(embeddedPaToken);
          collector.setApiToken(apiToken);
          collector.setPaUrl(embeddedPaUrl);
          collector.setCustomerUUID(customer.getUuid());
          collector.setMetricsUrl(prometheusUrl);
          collector.setMetricsUsername(metricsUsername);
          collector.setMetricsPassword(metricsPassword);
          collector.setYbaUrl(platformUrl);
          collector.setMetricsScrapePeriodSecs(scrapeInterval);
          collector.setEmbedded(true);
          perfAdvisorService.create(collector);
        } else if (StringUtils.isEmpty(embeddedPaUrl) && embeddedCollector != null) {
          log.info("Removing embedded collector for customer {}", customer.getUuid());
          // Delete the local yugaware DB row only - if we don't have embeddedPaUrl - this
          // means that embedded collector is not running and we can't call it's APIs anyway
          embeddedCollector.delete();
          for (Universe universe : Universe.getAllWithoutResources(customer)) {
            if (universe.getUniverseDetails().getPaCollectorUuid() != null
                && universe
                    .getUniverseDetails()
                    .getPaCollectorUuid()
                    .equals(embeddedCollector.getUuid())) {
              Universe.saveDetails(
                  universe.getUniverseUUID(), u -> u.getUniverseDetails().setPaCollectorUuid(null));
            }
          }
        } else {
          // Always update. On the leader this pushes YBA-side changes (URLs, scrape
          // interval, api tokens) to PA. On a follower it re-points the mirrored row at
          // this YBA's LOCAL PA and re-PUTs customer_metadata with collection_enabled=false
          // (see PerfAdvisorClient.putCustomerMetadata, which reads
          // HighAvailabilityConfig.isFollower()) - both mandatory so a role flip that
          // doesn't restart YBA propagates promptly. The row write is clobbered by the
          // next HA sync, but the customer_metadata PUT on the local PA is what actually
          // gates its scraping / anomaly detection.
          log.info("Updating embedded collector for customer {}", customer.getUuid());
          embeddedCollector.setPaApiToken(embeddedPaToken);
          embeddedCollector.setPaUrl(embeddedPaUrl);
          embeddedCollector.setMetricsUrl(prometheusUrl);
          embeddedCollector.setMetricsUsername(metricsUsername);
          embeddedCollector.setMetricsPassword(metricsPassword);
          embeddedCollector.setYbaUrl(platformUrl);
          embeddedCollector.setMetricsScrapePeriodSecs(scrapeInterval);
          perfAdvisorService.save(embeddedCollector, true);
        }
        metricService.setOkStatusMetric(
            buildMetricTemplate(PlatformMetrics.PA_EMBEDDED_COLLECTOR_INIT_STATUS, customer));
      } catch (Exception e) {
        log.error("Failed to sync PA collectors for customer {}", customer.getUuid(), e);
        metricService.setFailureStatusMetric(
            buildMetricTemplate(PlatformMetrics.PA_EMBEDDED_COLLECTOR_INIT_STATUS, customer));
      }
    }
  }

  /**
   * Brings each collector's export configs in line with the Perf Advisor Endpoints the universes
   * registered to it actually use.
   *
   * <p>Pushes only what changed. Every push makes the collector open a connection to the
   * destination, so a tick where nothing moved issues no requests at all; the state that makes that
   * possible is {@link #lastSynced}, and the one full pass per process start is what repairs drift
   * this class cannot otherwise see.
   */
  private void syncExportConfigs(Customer customer, boolean isFollower) {
    if (isFollower) {
      // A follower's embedded PA is a PA standby, and Perf Advisor refuses export config writes
      // anywhere but its leader. The standby gets them through PA's own HA replication instead.
      return;
    }
    if (!confGetter.getConfForScope(customer, CustomerConfKeys.enablePaOnlineMode)) {
      return;
    }

    List<PerfAdvisorEndpoint> endpoints = endpointService.list(customer.getUuid());
    Map<UUID, PerfAdvisorEndpoint> byUuid =
        endpoints.stream()
            .collect(Collectors.toMap(PerfAdvisorEndpoint::getUuid, Function.identity()));

    Set<Universe> universes = Universe.getAllWithoutResources(customer);
    for (PACollector collector :
        perfAdvisorService.list(
            PACollectorFilter.builder().customerUuid(customer.getUuid()).build())) {
      Set<UUID> needed = new HashSet<>();
      for (Universe universe : universes) {
        UUID collectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
        UUID endpointUuid = universe.getUniverseDetails().getPaEndpointUuid();
        if (collector.getUuid().equals(collectorUuid) && endpointUuid != null) {
          needed.add(endpointUuid);
        }
      }

      for (UUID endpointUuid : needed) {
        PerfAdvisorEndpoint endpoint = byUuid.get(endpointUuid);
        if (endpoint == null) {
          log.warn(
              "Universe on collector {} references Perf Advisor Endpoint {}, which no longer"
                  + " exists",
              collector.getUuid(),
              endpointUuid);
          continue;
        }
        CollectorEndpoint key = new CollectorEndpoint(collector.getUuid(), endpointUuid);
        Date synced = lastSynced.get(key);
        if (synced != null && !endpoint.getUpdateTime().after(synced)) {
          continue;
        }
        try {
          endpointService.push(collector, endpoint);
          // Recorded only on success, so a failure is retried on the next tick.
          lastSynced.put(key, endpoint.getUpdateTime());
          log.info(
              "Synced Perf Advisor Endpoint {} to collector {}", endpointUuid, collector.getUuid());
        } catch (Exception e) {
          log.warn(
              "Failed to sync Perf Advisor Endpoint {} to collector {}",
              endpointUuid,
              collector.getUuid(),
              e);
        }
      }

      if (reconciledCollectors.contains(collector.getUuid())) {
        continue;
      }
      try {
        removeOrphanedExportConfigs(collector, byUuid.keySet(), needed);
        reconciledCollectors.add(collector.getUuid());
      } catch (Exception e) {
        log.warn("Failed to reconcile export configs on collector {}", collector.getUuid(), e);
      }
    }
  }

  /**
   * Drops export configs this YBA put on the collector and no longer needs.
   *
   * <p>Only ids that match one of this customer's endpoints are touched: a Perf Advisor operator
   * can add an export config of their own through the PA UI, and sync must not treat that as
   * garbage.
   */
  private void removeOrphanedExportConfigs(
      PACollector collector, Set<UUID> knownEndpoints, Set<UUID> needed) {
    perfAdvisorService.listExportConfigs(collector).stream()
        .map(PerfAdvisorClient.ExportConfig::getId)
        .filter(id -> knownEndpoints.contains(id) && !needed.contains(id))
        .forEach(
            id -> {
              log.info(
                  "Removing unused export config {} from collector {}", id, collector.getUuid());
              endpointService.removeIfUnused(collector, id);
              lastSynced.remove(new CollectorEndpoint(collector.getUuid(), id));
            });
  }
}
