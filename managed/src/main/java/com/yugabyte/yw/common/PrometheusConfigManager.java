/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static org.flywaydb.play.FileUtils.readFileToString;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.operator.OperatorConfig;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.StringWriter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.velocity.Template;
import org.apache.velocity.VelocityContext;
import org.apache.velocity.app.VelocityEngine;
import org.apache.velocity.runtime.RuntimeConstants;
import org.apache.velocity.runtime.resource.loader.ClasspathResourceLoader;
import org.yaml.snakeyaml.Yaml;

@Singleton
@Slf4j
public class PrometheusConfigManager {

  private final KubernetesManagerFactory kubernetesManagerFactory;

  private final PrometheusConfigHelper prometheusConfigHelper;

  private static final String SCRAPE_CONFIG_TEMPLATE =
      "metric/k8s-node-scrape-config.yaml.template";

  private VelocityEngine velocityEngine;

  @Inject
  public PrometheusConfigManager(
      PrometheusConfigHelper prometheusConfigHelper,
      KubernetesManagerFactory kubernetesManagerFactory) {
    this.kubernetesManagerFactory = kubernetesManagerFactory;
    this.prometheusConfigHelper = prometheusConfigHelper;

    velocityEngine = new VelocityEngine();
    velocityEngine.setProperty(RuntimeConstants.RESOURCE_LOADER, "classpath");
    velocityEngine.setProperty(
        "classpath.resource.loader.class", ClasspathResourceLoader.class.getName());
    velocityEngine.init();
  }

  /**
   * Update the Prometheus scrape config entries for all the Kubernetes providers in the background.
   */
  public void updateK8sScrapeConfigs() {
    boolean COMMUNITY_OP_ENABLED = OperatorConfig.getOssMode();
    if (COMMUNITY_OP_ENABLED) {
      log.info("Skipping Prometheus config update as community edition is enabled");
      return;
    }
    Thread syncThread =
        new Thread(
            () -> {
              try {
                log.info("Trying to update scrape config entries for Kubernetes providers");
                updateK8sScrapeConfigsNow();
                log.info("Successfully updated Kubernetes scrape configs");
              } catch (Exception e) {
                log.warn("Failed to update Kubernetes scrape configs", e);
              }
            });
    syncThread.start();
  }

  public synchronized void updateK8sScrapeConfigsNow() {
    if (HighAvailabilityConfig.isFollower()) {
      log.info("Running in follower mode. Skipping Prometheus config update");
      return;
    }

    // Load the Prometheus configuration file and delete the old
    // generated entries.
    File promConfigFile = prometheusConfigHelper.getPrometheusConfigFile();
    Yaml yaml = new Yaml();
    Map<String, Object> promConfig = yaml.load(readFileToString(promConfigFile));
    List<Map<String, Object>> scrapeConfigs =
        (List<Map<String, Object>>) promConfig.get("scrape_configs");
    scrapeConfigs.removeIf(
        sc -> ((String) sc.getOrDefault("job_name", "")).startsWith("yba-generated-"));

    // Create scrape_config entry for each Kubernetes info
    Map<String, KubernetesInfo> k8sInfos = KubernetesUtil.getAllKubernetesInfos();
    if (k8sInfos.isEmpty()) {
      log.info("No Kubernetes infos found. Skipping Prometheus config update");
      return;
    }
    List<Map<String, Object>> k8sScrapeConfigs = new ArrayList<Map<String, Object>>();
    KubernetesManager k8s = kubernetesManagerFactory.getManager();
    // To avoid duplicate scrape targets, we keep track of APIServer
    // Endpoints of the visited clusters.
    HashSet visitedClusters = new HashSet<>();

    // TODO: make these class variables and initialize in the
    // constructor?
    Template template = velocityEngine.getTemplate(SCRAPE_CONFIG_TEMPLATE);
    VelocityContext context = new VelocityContext();

    for (Map.Entry<String, KubernetesInfo> entry : k8sInfos.entrySet()) {
      String uuid = entry.getKey();
      KubernetesInfo k8sInfo = entry.getValue();

      String apiServerEndpoint = k8sInfo.getApiServerEndpoint();
      // Skip the KubernetesInfo if we have successfully processed it before.
      if (visitedClusters.contains(apiServerEndpoint)) {
        continue;
      }

      String kubeConfigFile = k8sInfo.getKubeConfig();
      String tokenFile = k8sInfo.getKubeConfigTokenFile();
      String caFile = k8sInfo.getKubeConfigCAFile();

      // Only blank kubeconfig indicates that in-cluster credentials
      // are used. null kubeconfig means that this kubernetesinfo is
      // unused and some other kubernetesinfo from provider, zone, or
      // region is in use.
      if (StringUtils.isBlank(kubeConfigFile) && kubeConfigFile != null) {
        log.debug("Skipping home cluster Kubernetes info: {}", uuid);
        continue;
      }
      if (StringUtils.isAnyBlank(kubeConfigFile, apiServerEndpoint, tokenFile, caFile)) {
        log.debug("Skipping Kubernetes info due to missing authentication data: {}", uuid);
        continue;
      }

      // Skip the Kubernetes info if it is pointing to same cluster
      // where YBA is running. Prometheus already has scrape config
      // for the home cluster.
      try {
        if (k8s.isHomeCluster(kubeConfigFile)) {
          log.debug("Skipping home cluster Kubernetes info: {}", uuid);
          visitedClusters.add(apiServerEndpoint);
          continue;
        }
        // We skip the Kubernetes infos in case of failure to avoid any
        // duplicate metrics. We don't consider those as visited as we
        // might get a different authentication data in later iteration.
      } catch (Exception e) {
        log.warn(
            "Skipping Kubernetes info {}: Failed to verify if it is of the home cluster: {}",
            uuid,
            e.getMessage());
        continue;
      }
      visitedClusters.add(apiServerEndpoint);

      String apiServerScheme;
      // TODO(bhavin192): switch to java.net.URI/URL here? It doesn't
      // seem to work with just IPs without a protocol.
      String[] apiServerParts = apiServerEndpoint.split("://", 2);
      // APIServer endpoint without a scheme
      if (apiServerParts.length < 2) {
        apiServerEndpoint = apiServerParts[0];
        apiServerScheme = "http";
      } else {
        apiServerScheme = apiServerParts[0];
        apiServerEndpoint = apiServerParts[1];
      }

      String scrapeConfig = "";
      context.put("kubeconfig_uuid", uuid);
      context.put("scheme", apiServerScheme);
      context.put("server_domain", apiServerEndpoint);
      context.put("ca_file", caFile);
      context.put("bearer_token_file", tokenFile);
      context.put("kubeconfig_file", kubeConfigFile);
      try (StringWriter writer = new StringWriter()) {
        template.merge(context, writer);
        scrapeConfig = writer.toString();
      } catch (IOException ignored) {
        // Can't happen as it is from StringWriter's close, which does
        // nothing.
      }

      List<Map<String, Object>> scYaml = yaml.load(scrapeConfig);
      k8sScrapeConfigs.addAll(scYaml);
    }

    // Update Prometheus config with new Kubernetes scrape config
    // entries.
    scrapeConfigs.addAll(k8sScrapeConfigs);
    promConfig.put("scrape_configs", scrapeConfigs);

    try (BufferedWriter bw = new BufferedWriter(new FileWriter(promConfigFile))) {
      yaml.dump(promConfig, bw);
    } catch (IOException e) {
      log.error(e.getMessage());
      throw new RuntimeException("Error writing Prometheus configuration");
    }

    log.info("Wrote updated Prometheus configuration to disk");

    prometheusConfigHelper.reloadPrometheusConfig();
  }
}
