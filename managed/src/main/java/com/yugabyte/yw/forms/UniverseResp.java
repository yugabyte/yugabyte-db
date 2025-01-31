/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.AllowedTasks;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yaml.snakeyaml.Yaml;
import play.Environment;

@JsonInclude(JsonInclude.Include.NON_NULL)
// TODO is this description accurate?
//
@ApiModel(description = "Universe-creation response")
@Slf4j
public class UniverseResp {
  public static UniverseResp create(
      Universe universe, UUID taskUUID, RuntimeConfGetter confGetter) {
    UniverseResourceDetails.Context context = new Context(confGetter.getStaticConf(), universe);
    UniverseResourceDetails resourceDetails =
        UniverseResourceDetails.create(universe.getUniverseDetails(), context);
    fillRegions(Collections.singletonList(universe));
    // Universe UUID to AllowedTasks object map.
    Map<UUID, AllowedTasks> allowedTasks =
        getAllowedTasksOnFailure(Collections.singletonList(universe));
    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    boolean batchRollEnabled = UpgradeTaskBase.isBatchRollEnabled(universe, confGetter);
    if (batchRollEnabled) {
      rollMaxBatchSize = UpgradeTaskBase.getMaxNodesToRoll(universe);
    }
    return new UniverseResp(
        universe,
        taskUUID,
        resourceDetails,
        allowedTasks.get(universe.getUniverseUUID()),
        rollMaxBatchSize);
  }

  public static List<UniverseResp> create(
      Customer customer, Collection<Universe> universeList, RuntimeConfGetter confGetter) {
    List<UniverseDefinitionTaskParams> universeDefinitionTaskParams =
        universeList.stream().map(Universe::getUniverseDetails).collect(Collectors.toList());
    UniverseResourceDetails.Context context =
        new Context(confGetter.getStaticConf(), customer, universeDefinitionTaskParams);
    fillRegions(universeList);
    // Universe UUID to AllowedTasks object map.
    Map<UUID, AllowedTasks> allowedTasks = getAllowedTasksOnFailure(universeList);
    Map<UUID, RollMaxBatchSize> suggests = new HashMap<>();
    universeList.forEach(
        u -> {
          if (confGetter.getConfForScope(u, UniverseConfKeys.upgradeBatchRollEnabled)) {
            suggests.put(u.getUniverseUUID(), UpgradeTaskBase.getMaxNodesToRoll(u));
          }
        });

    return universeList.stream()
        .map(
            universe ->
                new UniverseResp(
                    universe,
                    null,
                    customer,
                    context.getProvider(
                        UUID.fromString(
                            universe.getUniverseDetails().getPrimaryCluster().userIntent.provider)),
                    UniverseResourceDetails.create(universe.getUniverseDetails(), context),
                    allowedTasks.get(universe.getUniverseUUID()),
                    suggests.get(universe.getUniverseUUID())))
        .collect(Collectors.toList());
  }

  private static void fillRegions(Collection<Universe> universes) {
    fillClusterRegions(
        universes.stream()
            .flatMap(universe -> universe.getUniverseDetails().clusters.stream())
            .collect(Collectors.toList()));
  }

  private static Map<UUID, AllowedTasks> getAllowedTasksOnFailure(Collection<Universe> universes) {
    // Universe UUID to placement modification task UUID map.
    Map<UUID, UUID> placementModificationTaskUuids =
        universes.stream()
            .filter(u -> u.getUniverseDetails().placementModificationTaskUuid != null)
            .collect(
                Collectors.toMap(
                    u -> u.getUniverseDetails().placementModificationTaskUuid,
                    u -> u.getUniverseUUID(),
                    (u1, u2) -> u2));
    // Fetch all the task info records in one shot for performance.
    return TaskInfo.find(placementModificationTaskUuids.keySet()).stream()
        .filter(t -> placementModificationTaskUuids.containsKey(t.getUuid()))
        .collect(
            Collectors.toMap(
                t -> placementModificationTaskUuids.get(t.getUuid()),
                t -> UniverseTaskBase.getAllowedTasksOnFailure(t)));
  }

  public static void fillClusterRegions(List<Cluster> clusters) {
    Set<UUID> regionUuids =
        clusters.stream()
            .filter(cluster -> CollectionUtils.isNotEmpty(cluster.userIntent.regionList))
            .flatMap(cluster -> cluster.userIntent.regionList.stream())
            .collect(Collectors.toSet());
    if (CollectionUtils.isEmpty(regionUuids)) {
      return;
    }
    Map<UUID, Region> regionMap =
        Region.findByUuids(regionUuids).stream()
            .collect(Collectors.toMap(region -> region.getUuid(), Function.identity()));
    clusters.stream()
        .filter(cluster -> CollectionUtils.isNotEmpty(cluster.userIntent.regionList))
        .forEach(
            cluster -> {
              cluster.regions =
                  cluster.userIntent.regionList.stream()
                      .filter(regionMap::containsKey)
                      .map(regionMap::get)
                      .collect(Collectors.toList());
            });
  }

  @ApiModelProperty(value = "Universe UUID")
  public final UUID universeUUID;

  @ApiModelProperty(value = "Universe name")
  public final String name;

  @ApiModelProperty(value = "Universe creation date")
  public final String creationDate;

  @ApiModelProperty(value = "Universe version")
  public final int version;

  @ApiModelProperty(value = "DNS name")
  public final String dnsName;

  @ApiModelProperty(value = "Universe resource details")
  public final UniverseResourceDetails resources;

  @ApiModelProperty(value = "Universe details")
  public final UniverseDefinitionTaskParamsResp universeDetails;

  @ApiModelProperty(value = "Universe configuration")
  public final Map<String, String> universeConfig;

  @ApiModelProperty(value = "Task UUID")
  public final UUID taskUUID;

  @ApiModelProperty(value = "Sample command")
  public final String sampleAppCommandTxt;

  @ApiModelProperty(value = "UUIDs of DR configs where this universe is the source (primary)")
  public final Set<UUID> drConfigUuidsAsSource;

  @ApiModelProperty(value = "UUIDs of DR configs where this universe is the target (secondary)")
  public final Set<UUID> drConfigUuidsAsTarget;

  @ApiModelProperty(
      value = "WARNING: This is a preview API that could change. Allowed tasks on the universe")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2024.1.0.0")
  public final AllowedUniverseTasksResp allowedTasks;

  @ApiModelProperty(
      value = "YbaApi Internal. Suggested number of tservers to roll during upgrade if available")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.0.0")
  public final RollMaxBatchSize rollMaxBatchSize;

  public UniverseResp(Universe entity) {
    this(entity, null, null, null, null);
  }

  public UniverseResp(Universe entity, UUID taskUUID) {
    this(entity, taskUUID, null, null, null);
  }

  public UniverseResp(
      Universe entity,
      UUID taskUUID,
      UniverseResourceDetails resources,
      AllowedTasks allowedTasks,
      RollMaxBatchSize rollMaxBatchSize) {
    this(
        entity,
        taskUUID,
        Customer.get(entity.getCustomerId()),
        Provider.getOrBadRequest(
            UUID.fromString(entity.getUniverseDetails().getPrimaryCluster().userIntent.provider)),
        resources,
        allowedTasks,
        rollMaxBatchSize);
  }

  public UniverseResp(
      Universe entity,
      UUID taskUUID,
      Customer customer,
      Provider provider,
      UniverseResourceDetails resources,
      AllowedTasks allowedTasks,
      RollMaxBatchSize rollMaxBatchSize) {
    universeUUID = entity.getUniverseUUID();
    name = entity.getName();
    creationDate = entity.getCreationDate().toString();
    version = entity.getVersion();
    dnsName = getDnsName(customer, provider);
    universeDetails = new UniverseDefinitionTaskParamsResp(entity.getUniverseDetails(), entity);
    this.taskUUID = taskUUID;
    this.resources = resources;
    universeConfig = entity.getConfig();
    this.sampleAppCommandTxt = this.getManifest(entity);
    this.drConfigUuidsAsSource =
        DrConfig.getBySourceUniverseUuid(universeUUID).stream()
            .map(DrConfig::getUuid)
            .collect(Collectors.toSet());
    this.drConfigUuidsAsTarget =
        DrConfig.getByTargetUniverseUuid(universeUUID).stream()
            .map(DrConfig::getUuid)
            .collect(Collectors.toSet());
    this.allowedTasks = allowedTasks == null ? null : new AllowedUniverseTasksResp(allowedTasks);

    this.rollMaxBatchSize = rollMaxBatchSize;
  }

  // TODO(UI folks): Remove this. This is redundant as it is already available in resources
  @ApiModelProperty(value = "Price")
  public Double getPricePerHour() {
    return resources == null ? null : resources.pricePerHour;
  }

  public String getDnsName(Customer customer, Provider provider) {
    if (provider == null) {
      return null;
    }
    String dnsSuffix = provider.getHostedZoneName();
    if (dnsSuffix == null) {
      return null;
    }
    return String.format("%s.%s.%s", name, customer.getCode(), dnsSuffix);
  }

  /** Returns the command to run the sample apps in the universe. */
  private String getManifest(Universe universe) {
    Set<NodeDetails> nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    Integer yqlServerRpcPort = universe.getUniverseDetails().communicationPorts.yqlServerRpcPort;
    StringBuilder nodeBuilder = new StringBuilder();
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    String sampleAppCommand;
    if (cluster.userIntent.providerType == null) {
      return null;
    }
    boolean isKubernetesProvider =
        cluster.userIntent.providerType.equals(Common.CloudType.kubernetes);
    // Building --nodes param value of the command
    nodeDetailsSet.stream()
        .filter(
            nodeDetails ->
                (nodeDetails.isTserver
                    && nodeDetails.state != null
                    && nodeDetails.state.name().equals("Live")))
        .forEach(
            nodeDetails ->
                nodeBuilder.append(
                    String.format(
                        nodeBuilder.length() == 0 ? "%s:%s" : ",%s:%s",
                        nodeDetails.cloudInfo.private_ip,
                        yqlServerRpcPort)));
    // If node to client TLS is enabled.
    if (cluster.userIntent.enableClientToNodeEncrypt) {
      String randomFileName = UUID.randomUUID().toString();
      UUID certUUID = universe.getUniverseDetails().getClientRootCA();
      if (certUUID == null) {
        log.warn("CertUUID cannot be null when TLS is enabled");
      }
      if (isKubernetesProvider) {
        Environment environment = StaticInjectorHolder.injector().instanceOf(Environment.class);
        String certContent = certUUID == null ? "" : CertificateHelper.getCertPEM(certUUID);
        Yaml yaml = new Yaml();
        String sampleAppCommandTxt =
            yaml.dump(
                yaml.load(
                    environment.resourceAsStream("templates/k8s-sample-app-command-pod.yml")));
        sampleAppCommandTxt =
            sampleAppCommandTxt
                .replace("<root_cert_content>", certContent)
                .replace("<nodes>", nodeBuilder.toString());

        String secretCommandTxt =
            yaml.dump(
                yaml.load(
                    environment.resourceAsStream("templates/k8s-sample-app-command-secret.yml")));
        secretCommandTxt =
            secretCommandTxt
                .replace("<root_cert_content>", certContent)
                .replace("<nodes>", nodeBuilder.toString());
        sampleAppCommandTxt = secretCommandTxt + "\n---\n" + sampleAppCommandTxt;
        sampleAppCommand = "echo -n \"" + sampleAppCommandTxt + "\" | kubectl create -f -";
      } else {
        // should we change this? Given that it can run on any host.
        sampleAppCommand =
            ("export FILE_NAME=/tmp/<file_name>.crt "
                    + "&& echo -n \"<root_cert_content>\" > $FILE_NAME "
                    + "&& docker run -d -v $FILE_NAME:/home/root.crt:ro yugabytedb/yb-sample-apps "
                    + "--workload CassandraKeyValue --nodes <nodes> --ssl_cert /home/root.crt")
                .replace(
                    "<root_cert_content>",
                    certUUID == null ? "" : CertificateHelper.getCertPEMFileContents(certUUID))
                .replace("<nodes>", nodeBuilder.toString());
      }
      sampleAppCommand = sampleAppCommand.replace("<file_name>", randomFileName);
    } else {
      // If TLS is disabled.
      if (isKubernetesProvider) {
        String commandTemplateKubeCtl =
            "kubectl run "
                + "--image=yugabytedb/yb-sample-apps yb-sample-apps "
                + "-- --workload CassandraKeyValue --nodes <nodes>";
        sampleAppCommand = commandTemplateKubeCtl.replace("<nodes>", nodeBuilder.toString());
      } else {
        String commandTemplateDocker =
            "docker run "
                + "-d yugabytedb/yb-sample-apps "
                + "--workload CassandraKeyValue --nodes <nodes>";
        sampleAppCommand = commandTemplateDocker.replace("<nodes>", nodeBuilder.toString());
      }
    }
    return sampleAppCommand;
  }
}
