/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Collection;
import java.util.HashSet;
import java.util.UUID;

import static com.yugabyte.yw.cloud.PublicCloudConstants.*;

public class UniverseResourceDetails {
  public static final Logger LOG = LoggerFactory.getLogger(UniverseResourceDetails.class);

  public double pricePerHour = 0;
  public double ebsPricePerHour = 0;
  public double numCores = 0;
  public double memSizeGB = 0;
  public int volumeCount = 0;
  public int volumeSizeGB = 0;
  public int numNodes = 0;
  public HashSet<String> azList = new HashSet<>();

  public void addCostPerHour(double price) {
    pricePerHour += price;
  }

  public void addEBSCostPerHour(double ebsPrice) {
    ebsPricePerHour += ebsPrice;
  }

  public void addVolumeCount(double volCount) {
    volumeCount += volCount;
  }

  public void addMemSizeGB(double memSize) {
    memSizeGB += memSize;
  }

  public void addVolumeSizeGB(double volSize) {
    volumeSizeGB += volSize;
  }

  public void addAz(String azName) {
    azList.add(azName);
  }

  public void addNumCores(double cores) {
    numCores += cores;
  }

  public void addNumNodes(int numNodes) {
    this.numNodes += numNodes;
  }

  public void addPrice(UniverseDefinitionTaskParams params) {

    // Calculate price
    double hourlyPrice = 0.0;
    double hourlyEBSPrice = 0.0;
    UserIntent userIntent = params.getPrimaryCluster().userIntent;
    for (NodeDetails nodeDetails : params.nodeDetailsSet) {
      if (nodeDetails.placementUuid != null) {
        userIntent = params.getClusterByUuid(nodeDetails.placementUuid).userIntent;
      }
      Provider provider = Provider.get(UUID.fromString(userIntent.provider));
      if (!nodeDetails.isActive()) {
        continue;
      }
      Region region = Region.getByCode(provider, nodeDetails.cloudInfo.region);

      if (region == null) {
        continue;
      }
      PriceComponent instancePrice = PriceComponent.get(provider.uuid, region.code,
        userIntent.instanceType);
      if (instancePrice == null) {
        continue;
      }
      hourlyPrice += instancePrice.priceDetails.pricePerHour;

      // Add price of volumes if necessary
      // TODO: Remove aws check once GCP volumes are decoupled from "EBS" designation
      // TODO(wesley): gcp options?
      if (userIntent.deviceInfo.storageType != null &&
        userIntent.providerType.equals(Common.CloudType.aws)) {
        Integer numVolumes = userIntent.deviceInfo.numVolumes;
        Integer diskIops = userIntent.deviceInfo.diskIops;
        Integer volumeSize = userIntent.deviceInfo.volumeSize;
        PriceComponent sizePrice;
        switch (userIntent.deviceInfo.storageType) {
          case IO1:
            PriceComponent piopsPrice = PriceComponent.get(provider.uuid, region.code, IO1_PIOPS);
            sizePrice = PriceComponent.get(provider.uuid, region.code, IO1_SIZE);
            if (piopsPrice != null && sizePrice != null) {
              hourlyEBSPrice += (numVolumes * (diskIops * piopsPrice.priceDetails.pricePerHour));
              hourlyEBSPrice += (numVolumes * (volumeSize * sizePrice.priceDetails.pricePerHour));
            }
            break;
          case GP2:
            sizePrice = PriceComponent.get(provider.uuid, region.code, GP2_SIZE);
            if (sizePrice != null) {
              hourlyEBSPrice += (numVolumes * volumeSize * sizePrice.priceDetails.pricePerHour);
            }
            break;
          default:
            break;
        }
      }
    }
    hourlyPrice += hourlyEBSPrice;

    // Add price to details
    addCostPerHour(Double.parseDouble(String.format("%.4f", hourlyPrice)));
    addEBSCostPerHour(Double.parseDouble(String.format("%.4f", hourlyEBSPrice)));
  }

  public static UniverseResourceDetails create(UniverseDefinitionTaskParams params) {
    return create(params.nodeDetailsSet, params);
  }

  /**
   * Create a UniverseResourceDetails object, which
   * contains info on the various pricing and
   * other sorts of resources used by this universe.
   *
   * @param nodes  Nodes that make up this universe.
   * @param params Parameters describing this universe.
   * @return a UniverseResourceDetails object containing info on the universe's resources.
   */
  public static UniverseResourceDetails create(Collection<NodeDetails> nodes,
                                               UniverseDefinitionTaskParams params) {
    UniverseResourceDetails details = new UniverseResourceDetails();
    for (Cluster cluster : params.clusters) {
      details.addNumNodes(cluster.userIntent.numNodes);
    }
    UserIntent userIntent = params.getPrimaryCluster().userIntent;
    for (NodeDetails node : nodes) {
      if (node.isActive()) {
        if (node.placementUuid != null) {
          userIntent = params.getClusterByUuid(node.placementUuid).userIntent;
        }
        if (userIntent.deviceInfo != null) {
          details.addVolumeCount(userIntent.deviceInfo.numVolumes);
          details.addVolumeSizeGB(
            userIntent.deviceInfo.volumeSize * userIntent.deviceInfo.numVolumes);
        }
        if (node.cloudInfo != null) {
          details.addAz(node.cloudInfo.az);
        InstanceType instanceType = InstanceType.get(UUID.fromString(userIntent.provider),
                node.cloudInfo.instance_type);
          if (instanceType == null) {
            LOG.error("Couldn't find instance type " + node.cloudInfo.instance_type +
              " for provider " + userIntent.providerType);
          } else {
            details.addMemSizeGB(instanceType.memSizeGB);
            details.addNumCores(instanceType.numCores);
          }
        }
      }
    }
    details.addPrice(params);
    return details;
  }
}
