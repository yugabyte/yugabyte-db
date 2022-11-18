// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;

/**
 * Represents all the details of a node that are got from the cloud provider.
 *
 * <p>NOTE: the names of fields in this class MUST correspond to the output field names of the
 * script 'yb_inventory.py' which is in the 'devops' repository.
 */
@ApiModel(description = "Node information reported by the cloud provider")
public class CloudSpecificInfo {
  // The private ip address
  @ApiModelProperty(value = "The node's private IP address")
  public String private_ip = null;

  // The secondary private ip address
  @ApiModelProperty(value = "Secondary Private IP")
  public String secondary_private_ip = null;

  // The public ip address.
  @ApiModelProperty(value = "The node's public IP address")
  public String public_ip = null;

  // The public dns name of the node.
  @ApiModelProperty(value = "The node's public DNS name")
  public String public_dns = null;

  // The private dns name of the node.
  @ApiModelProperty(value = "The node's private DNS")
  public String private_dns = null;

  // Type of the node (example: c3.xlarge on aws).
  @ApiModelProperty(value = "The node's instance type")
  public String instance_type = null;

  // The id of the subnet into which this node is deployed.
  @ApiModelProperty(value = "ID of the subnet on which this node is deployed")
  public String subnet_id = null;

  // The id of the secondary subnet into which this node is deployed.
  @ApiModelProperty(value = "Secondary Subnet IP")
  public String secondary_subnet_id = null;

  // The az into which the node is deployed.
  @ApiModelProperty(value = "The node's availability zone")
  public String az = null;

  // The region into which the node is deployed.
  @ApiModelProperty(value = "The node's region")
  public String region = null;

  // The cloud provider where the node is located.
  @ApiModelProperty(value = "The node's cloud provider")
  public String cloud = null;

  @ApiModelProperty(value = "True if the node has a public IP address assigned")
  public boolean assignPublicIP = true;

  @ApiModelProperty(value = "True if `use time sync` is enabled")
  public boolean useTimeSync = false;

  @ApiModelProperty(value = "Mount roots")
  public String mount_roots;

  @ApiModelProperty(value = "Mounted disks LUN indexes")
  public Integer[] lun_indexes = new Integer[0];

  @ApiModelProperty(value = "Pod name in Kubernetes")
  public String kubernetesPodName = null;

  @ApiModelProperty(value = "Kubernetes namespace")
  public String kubernetesNamespace = null;

  @ApiModelProperty(value = "Root volume ID or name")
  public String root_volume = null;

  public CloudSpecificInfo() {}

  @Override
  public CloudSpecificInfo clone() {
    CloudSpecificInfo cloudInfo = new CloudSpecificInfo();
    cloudInfo.private_ip = private_ip;
    cloudInfo.secondary_private_ip = secondary_private_ip;
    cloudInfo.public_ip = public_ip;
    cloudInfo.public_dns = public_dns;
    cloudInfo.private_dns = private_dns;
    cloudInfo.instance_type = instance_type;
    cloudInfo.subnet_id = subnet_id;
    cloudInfo.secondary_subnet_id = secondary_subnet_id;
    cloudInfo.az = az;
    cloudInfo.region = region;
    cloudInfo.cloud = cloud;
    cloudInfo.assignPublicIP = assignPublicIP;
    cloudInfo.mount_roots = mount_roots;
    cloudInfo.lun_indexes = lun_indexes == null ? new Integer[0] : lun_indexes.clone();
    cloudInfo.kubernetesPodName = kubernetesPodName;
    cloudInfo.kubernetesNamespace = kubernetesNamespace;
    cloudInfo.root_volume = root_volume;
    return cloudInfo;
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("cloudInfo: ")
        .append(az)
        .append(".")
        .append(region)
        .append(".")
        .append(cloud)
        .append(", type: ")
        .append(instance_type)
        .append(", ip: ")
        .append(private_ip);
    if (mount_roots != null && !mount_roots.isEmpty()) {
      sb.append(", mountRoots: ").append(mount_roots);
    }
    return sb.toString();
  }
}
