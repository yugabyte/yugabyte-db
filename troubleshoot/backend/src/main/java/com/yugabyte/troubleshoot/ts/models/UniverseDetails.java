/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.troubleshoot.ts.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import io.ebean.Model;
import io.ebean.annotation.DbJsonB;
import jakarta.validation.constraints.NotNull;
import java.time.Instant;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Data
@Entity
@EqualsAndHashCode(callSuper = false)
@Accessors(chain = true)
@Slf4j
public class UniverseDetails extends Model implements ModelWithId<UUID> {
  @NotNull @Id private UUID universeUUID;
  @NotNull private String name;
  @NotNull @DbJsonB private UniverseDefinition universeDetails;
  private Boolean lastSyncStatus;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant lastSyncTimestamp;

  @JsonFormat(pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'", timezone = "UTC")
  private Instant lastSuccessfulSyncTimestamp;

  private String lastSyncError;

  @Override
  public UUID getId() {
    return universeUUID;
  }

  @Data
  public static class UniverseDefinition {

    private List<Cluster> clusters;
    private String nodePrefix;
    private boolean updateInProgress;
    private boolean universePaused;
    public Set<NodeDetails> nodeDetailsSet;

    @Data
    public static class Cluster {
      private UUID uuid;
      private String clusterType;
      private UserIntent userIntent;
      private PlacementInfo placementInfo;
    }

    @Data
    public static class UserIntent {
      private String ybSoftwareVersion;
    }

    @Data
    public static class NodeDetails {
      private int nodeIdx;
      private String nodeName;
      private UUID nodeUuid;
      private UUID placementUuid;

      @JsonProperty(value = "isMaster")
      private boolean isMaster;

      @JsonProperty(value = "isTserver")
      private boolean isTserver;

      @JsonProperty(value = "isYsqlServer")
      private boolean isYsqlServer;

      private CloudSpecificInfo cloudInfo;

      @JsonIgnore
      public String getK8sPodName() {
        String pod = this.cloudInfo.kubernetesPodName;
        if (StringUtils.isBlank(pod)) {
          log.warn(
              "The pod name is blank for {}, inferring it from the first part of node private_ip",
              this.nodeName);
          if (StringUtils.isBlank(this.cloudInfo.private_ip)) {
            throw new RuntimeException(this.nodeName + " has a blank private_ip (FQDN)");
          }
          pod = this.cloudInfo.private_ip.split("\\.")[0];
        }
        return pod;
      }

      @JsonIgnore
      public String getK8sNamespace() {
        String namespace = this.cloudInfo.kubernetesNamespace;
        if (StringUtils.isBlank(namespace)) {
          log.warn(
              "The namesapce is blank for {}, inferring it from the third part of the node private_ip",
              this.nodeName);
          if (StringUtils.isBlank(this.cloudInfo.private_ip)) {
            throw new RuntimeException(this.nodeName + " has a blank private_ip (FQDN)");
          }
          namespace = this.cloudInfo.private_ip.split("\\.")[2];
        }
        return namespace;
      }
    }

    @Data
    public static class CloudSpecificInfo {
      private String private_ip;
      private String az;
      private String region;
      private String cloud;
      private String kubernetesPodName;
      private String kubernetesNamespace;
    }
  }

  public enum InstanceType {
    MASTER,
    TSERVER
  }
}
