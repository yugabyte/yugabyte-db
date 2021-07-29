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
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yaml.snakeyaml.Yaml;
import play.Play;

@JsonInclude(JsonInclude.Include.NON_NULL)
@ApiModel(description = "Universe Resp")
public class UniverseResp {

  public static final Logger LOG = LoggerFactory.getLogger(UniverseResp.class);

  public static UniverseResp create(Universe universe, UUID taskUUID, Config config) {
    UniverseResourceDetails resourceDetails =
        UniverseResourceDetails.create(universe.getUniverseDetails(), config);
    return new UniverseResp(universe, taskUUID, resourceDetails);
  }

  @ApiModelProperty(value = "Universe UUID")
  public final UUID universeUUID;

  @ApiModelProperty(value = "Universe name")
  public final String name;

  @ApiModelProperty(value = "Creation time")
  public final String creationDate;

  @ApiModelProperty(value = "Version")
  public final int version;

  @ApiModelProperty(value = "DNS name")
  public final String dnsName;

  @ApiModelProperty(value = "Universe Resources", dataType = "java.util.Map")
  public final UniverseResourceDetails resources;

  @ApiModelProperty(value = "Universe Details", dataType = "java.util.Map")
  public final UniverseDefinitionTaskParamsResp universeDetails;

  @ApiModelProperty(value = "Universe config")
  public final Map<String, String> universeConfig;

  @ApiModelProperty(value = "Task UUID")
  public final UUID taskUUID;

  @ApiModelProperty(value = "Sample command")
  public final String sampleAppCommandTxt;

  public UniverseResp(Universe entity) {
    this(entity, null, null);
  }

  public UniverseResp(Universe entity, UUID taskUUID) {
    this(entity, taskUUID, null);
  }

  public UniverseResp(Universe entity, UUID taskUUID, UniverseResourceDetails resources) {
    universeUUID = entity.universeUUID;
    name = entity.name;
    creationDate = entity.creationDate.toString();
    version = entity.version;
    dnsName = entity.getDnsName();
    universeDetails = new UniverseDefinitionTaskParamsResp(entity.getUniverseDetails(), entity);
    this.taskUUID = taskUUID;
    this.resources = resources;
    universeConfig = entity.getConfig();
    this.sampleAppCommandTxt = this.getManifest(entity);
  }

  // TODO(UI folks): Remove this. This is redundant as it is already available in resources
  @ApiModelProperty(value = "Price")
  public Double getPricePerHour() {
    return resources == null ? null : resources.pricePerHour;
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
    nodeDetailsSet
        .stream()
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
      UUID certUUID =
          universe.getUniverseDetails().rootAndClientRootCASame
              ? universe.getUniverseDetails().rootCA
              : universe.getUniverseDetails().clientRootCA;
      if (certUUID == null) {
        LOG.warn("!!! CertUUID cannot be null when TLS is enabled !!!");
      }
      if (isKubernetesProvider) {
        String certContent = certUUID == null ? "" : CertificateHelper.getCertPEM(certUUID);
        Yaml yaml = new Yaml();
        String sampleAppCommandTxt =
            yaml.dump(
                yaml.load(
                    Play.application()
                        .resourceAsStream("templates/k8s-sample-app-command-pod.yml")));
        sampleAppCommandTxt =
            sampleAppCommandTxt
                .replace("<root_cert_content>", certContent)
                .replace("<nodes>", nodeBuilder.toString());

        String secretCommandTxt =
            yaml.dump(
                yaml.load(
                    Play.application()
                        .resourceAsStream("templates/k8s-sample-app-command-secret.yml")));
        secretCommandTxt =
            secretCommandTxt
                .replace("<root_cert_content>", certContent)
                .replace("<nodes>", nodeBuilder.toString());
        sampleAppCommandTxt = secretCommandTxt + "\n---\n" + sampleAppCommandTxt;
        sampleAppCommand = "echo -n \"" + sampleAppCommandTxt + "\" | kubectl create -f -";
      } else {
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
