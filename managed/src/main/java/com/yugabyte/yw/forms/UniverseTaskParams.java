// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.KeyType;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.ebean.annotation.EnumValue;

import java.util.Set;
import java.util.UUID;

@ApiModel(value = "Universe task", description = "Universe task")
public class UniverseTaskParams extends AbstractTaskParams {

  @ApiModel(value = "Encryption at rest config", description = "Encryption at rest config")
  public static class EncryptionAtRestConfig {
    public enum OpType {
      @EnumValue("ENABLE")
      ENABLE,
      @EnumValue("DISABLE")
      DISABLE,
      @EnumValue("UNDEFINED")
      UNDEFINED;
    }

    // Whether a universe is currently encrypted at rest or not
    @ApiModelProperty(value = "Whether a universe is currently encrypted at rest or not")
    public boolean encryptionAtRestEnabled;

    // The KMS Configuration associated with the encryption keys being used on this universe
    @JsonAlias({"configUUID"})
    @ApiModelProperty(value = "KMS configuration")
    public UUID kmsConfigUUID;

    // Whether to enable/disable/rotate universe key/encryption at rest
    @JsonAlias({"key_op"})
    @ApiModelProperty(value = "Whether to enable/disable/rotate universe key/encryption at rest")
    public OpType opType;

    // Whether to generate a data key or just retrieve the CMK arn
    @JsonAlias({"key_type"})
    @ApiModelProperty(value = "Whether to generate a data key or just retrieve the CMK arn")
    public KeyType type;

    public EncryptionAtRestConfig() {
      this.encryptionAtRestEnabled = false;
      this.kmsConfigUUID = null;
      this.type = KeyType.DATA_KEY;
      this.opType = OpType.UNDEFINED;
    }

    public EncryptionAtRestConfig(EncryptionAtRestConfig config) {
      this.encryptionAtRestEnabled = config.encryptionAtRestEnabled;
      this.kmsConfigUUID = config.kmsConfigUUID;
      this.type = config.type;
      this.opType = config.opType;
    }

    public EncryptionAtRestConfig clone() {
      return new EncryptionAtRestConfig(this);
    }
  }

  @ApiModel(value = "Communication ports", description = "Communication ports")
  public static class CommunicationPorts {
    public CommunicationPorts() {
      // Set default port values.
      exportToCommunicationPorts(this);
    }

    // Ports that are customizable universe-wide.
    @ApiModelProperty(value = "HTTP port for master table")
    public int masterHttpPort;

    @ApiModelProperty(value = "RCP port for master table")
    public int masterRpcPort;

    @ApiModelProperty(value = "HTTP port for tserver")
    public int tserverHttpPort;

    @ApiModelProperty(value = "RPC port for tserver")
    public int tserverRpcPort;

    @ApiModelProperty(value = "HTTP port for redis")
    public int redisServerHttpPort;

    @ApiModelProperty(value = "RPC port for redis")
    public int redisServerRpcPort;

    @ApiModelProperty(value = "HTTP port for yql")
    public int yqlServerHttpPort;

    @ApiModelProperty(value = "RPC port for yql")
    public int yqlServerRpcPort;

    @ApiModelProperty(value = "HTTP port for ysql")
    public int ysqlServerHttpPort;

    @ApiModelProperty(value = "RPC port for ysql")
    public int ysqlServerRpcPort;

    @ApiModelProperty(value = "Node exporter port")
    public int nodeExporterPort;

    public static CommunicationPorts exportToCommunicationPorts(NodeDetails node) {
      return exportToCommunicationPorts(new CommunicationPorts(), node);
    }

    public static CommunicationPorts exportToCommunicationPorts(CommunicationPorts portsObj) {
      return exportToCommunicationPorts(portsObj, new NodeDetails());
    }

    public static CommunicationPorts exportToCommunicationPorts(
        CommunicationPorts portsObj, NodeDetails node) {
      portsObj.masterHttpPort = node.masterHttpPort;
      portsObj.masterRpcPort = node.masterRpcPort;
      portsObj.tserverHttpPort = node.tserverHttpPort;
      portsObj.tserverRpcPort = node.tserverRpcPort;
      portsObj.redisServerHttpPort = node.redisServerHttpPort;
      portsObj.redisServerRpcPort = node.redisServerRpcPort;
      portsObj.yqlServerHttpPort = node.yqlServerHttpPort;
      portsObj.yqlServerRpcPort = node.yqlServerRpcPort;
      portsObj.ysqlServerHttpPort = node.ysqlServerHttpPort;
      portsObj.ysqlServerRpcPort = node.ysqlServerRpcPort;
      portsObj.nodeExporterPort = node.nodeExporterPort;

      return portsObj;
    }

    public static void setCommunicationPorts(CommunicationPorts ports, NodeDetails node) {
      node.masterHttpPort = ports.masterHttpPort;
      node.masterRpcPort = ports.masterRpcPort;
      node.tserverHttpPort = ports.tserverHttpPort;
      node.tserverRpcPort = ports.tserverRpcPort;
      node.redisServerHttpPort = ports.redisServerHttpPort;
      node.redisServerRpcPort = ports.redisServerRpcPort;
      node.yqlServerHttpPort = ports.yqlServerHttpPort;
      node.yqlServerRpcPort = ports.yqlServerRpcPort;
      node.ysqlServerHttpPort = ports.ysqlServerHttpPort;
      node.ysqlServerRpcPort = ports.ysqlServerRpcPort;
      node.nodeExporterPort = ports.nodeExporterPort;
    }
  }

  @ApiModel(value = "Extra dependencies", description = "Extra dependencies")
  public static class ExtraDependencies {
    // Flag to install node_exporter on nodes.
    @ApiModelProperty(value = "Is install node exporter required")
    public boolean installNodeExporter = true;
  }

  // Which user to run the node exporter service on nodes with
  @ApiModelProperty(value = "Node exporter user")
  public String nodeExporterUser = "prometheus";

  // The primary device info.
  @ApiModelProperty(value = "Device information")
  public DeviceInfo deviceInfo;

  // The universe against which this operation is being executed.
  @ApiModelProperty(value = "Associate universe UUID")
  public UUID universeUUID;

  // Expected version of the universe for operation execution. Set to -1 if an operation should
  // not verify expected version of the universe.
  @ApiModelProperty(value = "Universe version")
  public Integer expectedUniverseVersion;

  // If an AWS backed universe has chosen EBS volume encryption, this will be set to the
  // Amazon Resource Name (ARN) of the CMK to be used to generate data keys for volume encryption
  @ApiModelProperty(value = "Amazon Resource Name (ARN) of the CMK")
  public String cmkArn;

  // Store encryption key provider specific configuration/authorization values
  @ApiModelProperty(value = "Encryption at rest configation")
  public EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();

  // The set of nodes that are part of this universe. Should contain nodes in both primary and
  // readOnly clusters.
  @ApiModelProperty(value = "Node details")
  public Set<NodeDetails> nodeDetailsSet = null;

  // A list of ports to configure different parts of YB to listen on.
  @ApiModelProperty(value = "Communication ports")
  public CommunicationPorts communicationPorts = new CommunicationPorts();

  // Dependencies that can be install on nodes or not
  @ApiModelProperty(value = "Extra dependencies")
  public ExtraDependencies extraDependencies = new ExtraDependencies();

  // Whether this task has been tried before or not. Awkward naming because we cannot use
  // `isRetry` due to play reading the "is" prefix differently.
  @ApiModelProperty(value = "Whether this task has been tried before or not")
  public boolean firstTry = true;
}
