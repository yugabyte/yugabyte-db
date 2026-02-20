package com.yugabyte.yw.common.pa;

import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class EmbeddedCollectorInitializer {

  private final SettableRuntimeConfigFactory configFactory;
  private final PerfAdvisorService perfAdvisorService;
  private final MetricUrlProvider metricUrlProvider;
  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public EmbeddedCollectorInitializer(
      SettableRuntimeConfigFactory configFactory,
      PerfAdvisorService perfAdvisorService,
      MetricUrlProvider metricUrlProvider,
      RoleBindingUtil roleBindingUtil) {
    this.configFactory = configFactory;
    this.perfAdvisorService = perfAdvisorService;
    this.metricUrlProvider = metricUrlProvider;
    this.roleBindingUtil = roleBindingUtil;
  }

  public void initialize() {
    String embeddedPaUrl = configFactory.staticApplicationConf().getString("yb.pa.url");
    String embeddedPaToken = configFactory.staticApplicationConf().getString("yb.pa.api_token");
    String platformUrl = configFactory.staticApplicationConf().getString("yb.platform.url");
    long scrapeInterval =
        SwamperHelper.getScrapeIntervalSeconds(configFactory.staticApplicationConf());
    String prometheusUrl = metricUrlProvider.getMetricsInternalUrl();

    Map<UUID, PACollector> allCollectors =
        perfAdvisorService.list(PACollectorFilter.builder().build()).stream()
            .collect(Collectors.toMap(PACollector::getUuid, Function.identity()));
    if (StringUtils.isEmpty(embeddedPaUrl) && allCollectors.isEmpty()) {
      return;
    }

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
    for (Customer customer : Customer.getAll()) {
      PACollector embeddedCollector = allCollectors.get(customer.getUuid());
      if (StringUtils.isEmpty(embeddedPaUrl) && embeddedCollector == null) {
        continue;
      } else if (StringUtils.isNotEmpty(embeddedPaUrl) && embeddedCollector == null) {
        List<PACollector> existing =
            perfAdvisorService.list(
                PACollectorFilter.builder()
                    .customerUuid(customer.getUuid())
                    .paUrl(embeddedPaUrl)
                    .build());

        if (!existing.isEmpty()) {
          log.info(
              "Embedded collector already exists for customer {}, skipping", customer.getUuid());
          continue;
        }

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
        perfAdvisorService.create(collector);
      } else if (StringUtils.isEmpty(embeddedPaUrl) && embeddedCollector != null) {
        log.info("Removing embedded collector for customer {}", customer.getUuid());
        perfAdvisorService.delete(customer.getUuid(), customer.getUuid(), true);
        for (Universe universe : Universe.getAllWithoutResources(customer)) {
          if (universe.getUniverseDetails().getPaCollectorUuid() != null
              && universe.getUniverseDetails().getPaCollectorUuid().equals(customer.getUuid())) {
            Universe.saveDetails(
                universe.getUniverseUUID(), u -> u.getUniverseDetails().setPaCollectorUuid(null));
          }
        }
      } else {
        if (embeddedCollector.getMetricsScrapePeriodSecs() != scrapeInterval
            || !embeddedCollector.getPaUrl().equals(embeddedPaUrl)
            || !embeddedCollector.getPaApiToken().equals(embeddedPaToken)
            || !embeddedCollector.getYbaUrl().equals(platformUrl)
            || !embeddedCollector.getMetricsUrl().equals(prometheusUrl)
            || !embeddedCollector.getMetricsUsername().equals(metricsUsername)
            || !embeddedCollector.getMetricsPassword().equals(metricsPassword)) {

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
      }
    }
  }
}
