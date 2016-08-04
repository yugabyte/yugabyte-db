// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

/**
 * Represents all the details of a cloud node that are of interest.
 *
 * NOTE: the names of fields in this class MUST correspond to the output field names of the script
 * 'find_cloud_host.sh' which is in the 'devops' repository.
 */
public class NodeDetails {
  // The id of the node. This is usually present in the node name.
  public int nodeIdx = -1;
  // Name of the node.
  public String instance_name;
  // Type of the node (example: c3.xlarge).
  public String instance_type;

  // The private ip address
  public String private_ip;
  // The public ip address.
  public String public_ip;
  // The public dns name of the node.
  public String public_dns;
  // The private dns name of the node.
  public String private_dns;

  // AWS only. The id of the subnet into which this node is deployed.
  public String subnet_id;
  // The az into which the node is deployed.
  public String az;
  // The region into which the node is deployed.
  public String region;
  // The cloud provider where the node is located.
  public String cloud;

  // True if this node is a master, along with port info.
  public boolean isMaster;
  public int masterHttpPort = 7000;
  public int masterRpcPort = 7100;

  // True if this node is a tserver, along with port info.
  public boolean isTserver;
  public int tserverHttpPort = 9000;
  public int tserverRpcPort = 9100;

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("name: ")
      .append(instance_name).append(".").append(az).append(".")
      .append(region).append(".").append(cloud)
      .append(", ip: ").append(private_ip)
      .append(", isMaster: ").append(isMaster)
      .append(", isTserver: ").append(isTserver);
    return sb.toString();
  }
}
