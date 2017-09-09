// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;

import java.util.Collection;
import java.util.HashSet;
import java.util.UUID;

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;

public class UniverseResourceDetails {
  public double pricePerHour = 0;
  public double ebsPricePerHour = 0;
  public int numCores = 0;
  public double memSizeGB = 0;
  public int volumeCount = 0;
  public int volumeSizeGB = 0;
  public int numNodes = 0;
  public HashSet<String> azList = new HashSet<>();

  public void addCostPerHour(double price) {
    pricePerHour += price;
  }

  public void addEBSCostPerHour(double price) {
    ebsPricePerHour += price;
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

  public void addNumCores(int cores) {
    numCores += cores;
  }

  public void addNumNodes(int numNodes) {
    this.numNodes = numNodes;
  }

  public void addPrice(UniverseDefinitionTaskParams params) {
    Provider provider = Provider.get(UUID.fromString(params.userIntent.provider));
    InstanceType instanceType = InstanceType.get(provider.code, params.userIntent.instanceType);

    // Calculate price
    double hourlyPrice = 0.0;
    double hourlyEBSPrice = 0.0;
    DeviceInfo deviceInfo = params.userIntent.deviceInfo;
    for (NodeDetails nodeDetails : params.nodeDetailsSet) {

      if (!nodeDetails.isActive()) {
        continue;
      }
      Region region = Region.getByCode(provider, nodeDetails.cloudInfo.region);

      // Add price of instance, using spotPrice if this universe is a spotPrice-based universe.
      if (params.cloud.equals(Common.CloudType.aws) && params.userIntent.spotPrice > 0.0) {
        hourlyPrice += params.userIntent.spotPrice;
      } else {
        PriceComponent instancePrice = PriceComponent.get(provider.code, region.code,
            instanceType.getInstanceTypeCode());
        if (instancePrice == null) {
          continue;
        }
        hourlyPrice += instancePrice.priceDetails.pricePerHour;
      }

      // Add price of volumes if necessary
      // TODO: Remove aws check once GCP volumes are decoupled from "EBS" designation
      if (deviceInfo.ebsType != null && params.cloud.equals(Common.CloudType.aws)) {
        Integer numVolumes = deviceInfo.numVolumes;
        Integer diskIops = deviceInfo.diskIops;
        Integer volumeSize = deviceInfo.volumeSize;
        PriceComponent sizePrice;
        switch (deviceInfo.ebsType) {
          case IO1:
            PriceComponent piopsPrice = PriceComponent.get(provider.code, region.code, IO1_PIOPS);
            sizePrice = PriceComponent.get(provider.code, region.code, IO1_SIZE);
            if (piopsPrice != null && sizePrice != null) {
              hourlyEBSPrice += (numVolumes * (diskIops * piopsPrice.priceDetails.pricePerHour));
              hourlyEBSPrice += (numVolumes * (volumeSize * sizePrice.priceDetails.pricePerHour));
            }
            break;
          case GP2:
            sizePrice = PriceComponent.get(provider.code, region.code, GP2_SIZE);
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
    if (deviceInfo.ebsType != null) {
      addEBSCostPerHour(Double.parseDouble(String.format("%.4f", hourlyEBSPrice)));
    }
  }

  public static UniverseResourceDetails create(Collection<NodeDetails> nodes,
                                               UniverseDefinitionTaskParams params) {
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.addNumNodes(params.userIntent.numNodes);
    Common.CloudType cloudType = params.userIntent.providerType;
    for (NodeDetails node : nodes) {
      if (node.isActive()) {
        ResourceUtil.mergeResourceDetails(
            params.userIntent.deviceInfo,
            cloudType,
            node.cloudInfo.instance_type,
            node.cloudInfo.az,
            details);
      }
    }
    details.addPrice(params);
    return details;
  }
}
