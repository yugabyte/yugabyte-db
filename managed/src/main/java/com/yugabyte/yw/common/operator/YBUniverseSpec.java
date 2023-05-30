// // Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.models.helpers.DeviceInfo;
import io.fabric8.kubernetes.api.model.Affinity;
import io.fabric8.kubernetes.api.model.ResourceRequirements;
import java.util.List;
import java.util.Map;

@com.fasterxml.jackson.annotation.JsonInclude(
    com.fasterxml.jackson.annotation.JsonInclude.Include.NON_NULL)
@com.fasterxml.jackson.annotation.JsonPropertyOrder({
  "accessKeyCode",
  "assignPublicIP",
  "dedicatedNodes",
  "enableClientToNodeEncrypt",
  "enableExposingService",
  "enableIPV6",
  "enableNodeToNodeEncrypt",
  "enableVolumeEncryption",
  "enableYCQL",
  "enableYCQLAuth",
  "enableYEDIS",
  "enableYSQL",
  "enableYSQLAuth",
  "instanceType",
  "numNodes",
  "providerName",
  "replicas",
  "replicationFactor",
  "universeName",
  "useSystemd",
  "useTimeSync",
  "version",
  "ybSoftwareVersion",
  "ycqlPassword",
  "ysqlPassword",
  "kubernetesOverrides",
  "deviceInfo",
})
@com.fasterxml.jackson.databind.annotation.JsonDeserialize(
    using = com.fasterxml.jackson.databind.JsonDeserializer.None.class)
@javax.annotation.Generated("io.fabric8.java.generator.CRGeneratorRunner")
public class YBUniverseSpec implements io.fabric8.kubernetes.api.model.KubernetesResource {

  @com.fasterxml.jackson.annotation.JsonProperty("accessKeyCode")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String accessKeyCode;

  public String getAccessKeyCode() {
    return accessKeyCode;
  }

  public void setAccessKeyCode(String accessKeyCode) {
    this.accessKeyCode = accessKeyCode;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("assignPublicIP")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean assignPublicIP;

  public Boolean getAssignPublicIP() {
    return assignPublicIP;
  }

  public void setAssignPublicIP(Boolean assignPublicIP) {
    this.assignPublicIP = assignPublicIP;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("dedicatedNodes")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean dedicatedNodes;

  public Boolean getDedicatedNodes() {
    return dedicatedNodes;
  }

  public void setDedicatedNodes(Boolean dedicatedNodes) {
    this.dedicatedNodes = dedicatedNodes;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableClientToNodeEncrypt")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableClientToNodeEncrypt;

  public Boolean getEnableClientToNodeEncrypt() {
    return enableClientToNodeEncrypt;
  }

  public void setEnableClientToNodeEncrypt(Boolean enableClientToNodeEncrypt) {
    this.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableExposingService")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String enableExposingService;

  public String getEnableExposingService() {
    return enableExposingService;
  }

  public void setEnableExposingService(String enableExposingService) {
    this.enableExposingService = enableExposingService;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableIPV6")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableIPV6;

  public Boolean getEnableIPV6() {
    return enableIPV6;
  }

  public void setEnableIPV6(Boolean enableIPV6) {
    this.enableIPV6 = enableIPV6;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableNodeToNodeEncrypt")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableNodeToNodeEncrypt;

  public Boolean getEnableNodeToNodeEncrypt() {
    return enableNodeToNodeEncrypt;
  }

  public void setEnableNodeToNodeEncrypt(Boolean enableNodeToNodeEncrypt) {
    this.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableVolumeEncryption")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableVolumeEncryption;

  public Boolean getEnableVolumeEncryption() {
    return enableVolumeEncryption;
  }

  public void setEnableVolumeEncryption(Boolean enableVolumeEncryption) {
    this.enableVolumeEncryption = enableVolumeEncryption;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableYCQL")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableYCQL;

  public Boolean getEnableYCQL() {
    return enableYCQL;
  }

  public void setEnableYCQL(Boolean enableYCQL) {
    this.enableYCQL = enableYCQL;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableYCQLAuth")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableYCQLAuth;

  public Boolean getEnableYCQLAuth() {
    return enableYCQLAuth;
  }

  public void setEnableYCQLAuth(Boolean enableYCQLAuth) {
    this.enableYCQLAuth = enableYCQLAuth;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableYEDIS")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableYEDIS;

  public Boolean getEnableYEDIS() {
    return enableYEDIS;
  }

  public void setEnableYEDIS(Boolean enableYEDIS) {
    this.enableYEDIS = enableYEDIS;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableYSQL")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableYSQL;

  public Boolean getEnableYSQL() {
    return enableYSQL;
  }

  public void setEnableYSQL(Boolean enableYSQL) {
    this.enableYSQL = enableYSQL;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("enableYSQLAuth")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean enableYSQLAuth;

  public Boolean getEnableYSQLAuth() {
    return enableYSQLAuth;
  }

  public void setEnableYSQLAuth(Boolean enableYSQLAuth) {
    this.enableYSQLAuth = enableYSQLAuth;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("instanceType")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String instanceType;

  public String getInstanceType() {
    return instanceType;
  }

  public void setInstanceType(String instanceType) {
    this.instanceType = instanceType;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("numNodes")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Long numNodes;

  public Long getNumNodes() {
    return numNodes;
  }

  public void setNumNodes(Long numNodes) {
    this.numNodes = numNodes;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("providerName")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String providerName;

  public String getProviderName() {
    return providerName;
  }

  public void setProviderName(String providerName) {
    this.providerName = providerName;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("replicas")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Long replicas;

  public Long getReplicas() {
    return replicas;
  }

  public void setReplicas(Long replicas) {
    this.replicas = replicas;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("replicationFactor")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Long replicationFactor;

  public Long getReplicationFactor() {
    return replicationFactor;
  }

  public void setReplicationFactor(Long replicationFactor) {
    this.replicationFactor = replicationFactor;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("universeName")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String universeName;

  public String getUniverseName() {
    return universeName;
  }

  public void setUniverseName(String universeName) {
    this.universeName = universeName;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("useSystemd")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean useSystemd;

  public Boolean getUseSystemd() {
    return useSystemd;
  }

  public void setUseSystemd(Boolean useSystemd) {
    this.useSystemd = useSystemd;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("useTimeSync")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Boolean useTimeSync;

  public Boolean getUseTimeSync() {
    return useTimeSync;
  }

  public void setUseTimeSync(Boolean useTimeSync) {
    this.useTimeSync = useTimeSync;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("version")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private Long version;

  public Long getVersion() {
    return version;
  }

  public void setVersion(Long version) {
    this.version = version;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("ybSoftwareVersion")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String ybSoftwareVersion;

  public String getYbSoftwareVersion() {
    return ybSoftwareVersion;
  }

  public void setYbSoftwareVersion(String ybSoftwareVersion) {
    this.ybSoftwareVersion = ybSoftwareVersion;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("ycqlPassword")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String ycqlPassword;

  public String getYcqlPassword() {
    return ycqlPassword;
  }

  public void setYcqlPassword(String ycqlPassword) {
    this.ycqlPassword = ycqlPassword;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("ysqlPassword")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private String ysqlPassword;

  public String getYsqlPassword() {
    return ysqlPassword;
  }

  public void setYsqlPassword(String ysqlPassword) {
    this.ysqlPassword = ysqlPassword;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("kubernetesOverrides")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private KubernetesOverrides kubernetesOverrides;

  public static class KubernetesOverrides {
    public static class TServer {
      public Affinity affinity;
    }

    public static class Master {
      public Affinity affinity;
    }

    public static class Resource {
      public ResourceRequirements tserver;
      public ResourceRequirements master;
    }

    public static class ServiceEndpoints {
      public String name;
      public String type;
      public Map<String, String> annotations;
      public String app;
      public Map<String, String> ports;
    }

    public TServer tserver;
    public Master master;
    public Resource resource;
    public Map<String, String> nodeSelector;
    public List<ServiceEndpoints> serviceEndpoints;
  }

  public KubernetesOverrides getKubernetesOverrides() {
    return kubernetesOverrides;
  }

  public void setKubernetesOverrides(KubernetesOverrides kubernetesOverrides) {
    this.kubernetesOverrides = kubernetesOverrides;
  }

  @com.fasterxml.jackson.annotation.JsonProperty("deviceInfo")
  @com.fasterxml.jackson.annotation.JsonSetter(nulls = com.fasterxml.jackson.annotation.Nulls.SKIP)
  private DeviceInfo deviceInfo;

  public DeviceInfo getDeviceInfo() {
    return deviceInfo;
  }

  public void setDeviceInfo(DeviceInfo deviceInfo) {
    this.deviceInfo = deviceInfo;
  }
}
