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

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_THROUGHPUT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceTypeKey;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.PriceComponentKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.ProviderAndRegion;
import io.swagger.annotations.ApiModelProperty;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.Value;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class UniverseResourceDetails {
  public static final int MIB_IN_GIB = 1024;
  public static final String GP3_FREE_PIOPS_PARAM = "yb.aws.storage.gp3_free_piops";
  public static final String GP3_FREE_THROUGHPUT_PARAM = "yb.aws.storage.gp3_free_throughput";
  public static final Logger LOG = LoggerFactory.getLogger(UniverseResourceDetails.class);
  private static final double EPSILON = 1E-6;

  @ApiModelProperty(value = "Price per hour")
  public double pricePerHour = 0;

  @ApiModelProperty(value = "EBS price per hour")
  public double ebsPricePerHour = 0;

  @ApiModelProperty(value = "Numbers of cores")
  public double numCores = 0;

  @ApiModelProperty(value = "Memory GB")
  public double memSizeGB = 0;

  @ApiModelProperty(value = "Volume count")
  public int volumeCount = 0;

  @ApiModelProperty(value = "Volume in GB")
  public int volumeSizeGB = 0;

  @ApiModelProperty(value = "Numbers of node")
  public int numNodes = 0;

  @ApiModelProperty(value = "gp3 free piops")
  public int gp3FreePiops;

  @ApiModelProperty(value = "gp3 free throughput")
  public int gp3FreeThroughput;

  @ApiModelProperty(value = "Azs")
  public HashSet<String> azList = new HashSet<>();

  @ApiModelProperty(value = "Known pricing info")
  public boolean pricingKnown = true;

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

  public void setPricingKnown(boolean val) {
    pricingKnown = val;
  }

  public void addPrice(UniverseDefinitionTaskParams params, Context context) {

    // Calculate price
    double hourlyPrice = 0.0;
    double hourlyEBSPrice = 0.0;
    UserIntent userIntent = params.getPrimaryCluster().userIntent;
    Map<Pair<String, String>, Double> spotPrices = new HashMap<>();
    for (NodeDetails nodeDetails : params.nodeDetailsSet) {
      if (nodeDetails.placementUuid != null) {
        userIntent = params.getClusterByUuid(nodeDetails.placementUuid).userIntent;
      }
      Provider provider = context.getProvider(UUID.fromString(userIntent.provider));
      Region region = context.getRegion(provider.getUuid(), nodeDetails.cloudInfo.region);

      if (region == null) {
        continue;
      }
      String instanceType = userIntent.getInstanceTypeForNode(nodeDetails);
      PriceComponent instancePrice =
          context.getPriceComponent(provider.getUuid(), region.getCode(), instanceType);
      if (instancePrice == null) {
        continue;
      }
      if (Math.abs(instancePrice.getPriceDetails().pricePerHour - 0) < EPSILON) {
        setPricingKnown(false);
      }
      if (!context.isNodeCounted(nodeDetails)) {
        continue;
      }

      DeviceInfo deviceInfo = userIntent.getDeviceInfoForNode(nodeDetails);

      // Add price of volumes if necessary
      // TODO: Remove aws check once GCP volumes are decoupled from "EBS" designation
      // TODO(wesley): gcp options?
      if (deviceInfo.storageType != null && userIntent.providerType.equals(Common.CloudType.aws)) {
        Integer numVolumes = deviceInfo.numVolumes;
        Integer diskIops = deviceInfo.diskIops;
        Integer volumeSize = deviceInfo.volumeSize;
        Integer throughput = deviceInfo.throughput;
        Integer billedDiskIops = null;
        Integer billedThroughput = null;
        PriceComponent sizePrice = null;
        PriceComponent piopsPrice = null;
        PriceComponent mibpsPrice = null;
        switch (deviceInfo.storageType) {
          case IO1:
            piopsPrice = PriceComponent.get(provider.getUuid(), region.getCode(), IO1_PIOPS);
            sizePrice = PriceComponent.get(provider.getUuid(), region.getCode(), IO1_SIZE);
            billedDiskIops = diskIops;
            break;
          case GP2:
            sizePrice = PriceComponent.get(provider.getUuid(), region.getCode(), GP2_SIZE);
            break;
          case GP3:
            piopsPrice = PriceComponent.get(provider.getUuid(), region.getCode(), GP3_PIOPS);
            sizePrice = PriceComponent.get(provider.getUuid(), region.getCode(), GP3_SIZE);
            mibpsPrice = PriceComponent.get(provider.getUuid(), region.getCode(), GP3_THROUGHPUT);
            billedDiskIops = diskIops > gp3FreePiops ? diskIops - gp3FreePiops : null;
            billedThroughput =
                throughput > gp3FreeThroughput ? throughput - gp3FreeThroughput : null;
            break;
          default:
            break;
        }
        if (sizePrice != null) {
          hourlyEBSPrice += (numVolumes * (volumeSize * sizePrice.getPriceDetails().pricePerHour));
        }
        if (piopsPrice != null && billedDiskIops != null) {
          hourlyEBSPrice +=
              (numVolumes * (billedDiskIops * piopsPrice.getPriceDetails().pricePerHour));
        }
        if (mibpsPrice != null && billedThroughput != null) {
          hourlyEBSPrice +=
              (numVolumes
                  * (billedThroughput * mibpsPrice.getPriceDetails().pricePerHour / MIB_IN_GIB));
        }
      }
      if (!params.universePaused) {
        // Node is in Stopped state when universe is paused and when node processes are stopped
        // - and we need to distinguish between the two.
        if (!userIntent.useSpotInstance) {
          hourlyPrice += instancePrice.getPriceDetails().pricePerHour;
        } else {
          if (userIntent.spotPrice > 0.0) {
            hourlyPrice += userIntent.spotPrice;
          } else {
            Pair<String, String> spotPair;
            Double spotPrice;
            switch (userIntent.providerType) {
              case aws:
                spotPair = new Pair<String, String>(nodeDetails.getZone(), instanceType);
                String providerUUID = userIntent.provider;
                spotPrice =
                    spotPrices.computeIfAbsent(
                        spotPair,
                        pair -> {
                          return AWSUtil.getAwsSpotPrice(
                              pair.getFirst(),
                              pair.getSecond(),
                              providerUUID,
                              nodeDetails.getRegion());
                        });
                break;
              case gcp:
                spotPair = new Pair<String, String>(nodeDetails.getRegion(), instanceType);
                spotPrice =
                    spotPrices.computeIfAbsent(
                        spotPair,
                        pair -> {
                          return GCPUtil.getGcpSpotPrice(pair.getFirst(), pair.getSecond());
                        });
                break;
              case azu:
                spotPair = new Pair<String, String>(nodeDetails.getRegion(), instanceType);
                spotPrice =
                    spotPrices.computeIfAbsent(
                        spotPair,
                        pair -> {
                          return AZUtil.getAzuSpotPrice(pair.getFirst(), pair.getSecond());
                        });
                break;
              default:
                spotPrice = Double.NaN;
            }
            hourlyPrice +=
                (!spotPrice.equals(Double.NaN)
                    ? spotPrice
                    : instancePrice.getPriceDetails().pricePerHour);
          }
        }
      }
    }
    hourlyPrice += hourlyEBSPrice;

    // Add price to details
    addCostPerHour(Double.parseDouble(String.format("%.4f", hourlyPrice)));
    addEBSCostPerHour(Double.parseDouble(String.format("%.4f", hourlyEBSPrice)));
  }

  public static UniverseResourceDetails create(
      UniverseDefinitionTaskParams params, Context context) {
    return create(params.nodeDetailsSet, params, context);
  }

  /**
   * Create a UniverseResourceDetails object, which contains info on the various pricing and other
   * sorts of resources used by this universe.
   *
   * @param nodes Nodes that make up this universe.
   * @param params Parameters describing this universe.
   * @return a UniverseResourceDetails object containing info on the universe's resources.
   */
  public static UniverseResourceDetails create(
      Collection<NodeDetails> nodes, UniverseDefinitionTaskParams params, Context context) {
    UniverseResourceDetails details = new UniverseResourceDetails();
    details.gp3FreePiops = context.getConfig().getInt(GP3_FREE_PIOPS_PARAM);
    details.gp3FreeThroughput = context.getConfig().getInt(GP3_FREE_THROUGHPUT_PARAM);

    for (Cluster cluster : params.clusters) {
      details.addNumNodes(cluster.userIntent.numNodes);
    }
    UserIntent userIntent = params.getPrimaryCluster().userIntent;
    for (NodeDetails node : nodes) {
      if (node.isActive()) {
        if (node.placementUuid != null) {
          userIntent = params.getClusterByUuid(node.placementUuid).userIntent;
        }
        if (userIntent.deviceInfo != null
            && userIntent.deviceInfo.volumeSize != null
            && userIntent.deviceInfo.numVolumes != null) {
          details.addVolumeCount(userIntent.deviceInfo.numVolumes);
          // Check correct volume size based of type of node
          if (node.isTserver) {
            details.addVolumeSizeGB(
                userIntent.deviceInfo.volumeSize * userIntent.deviceInfo.numVolumes);
          } else if (node.isMaster) {
            // if populated masterDeviceInfo use that, else use deviceInfo.
            if (userIntent.masterDeviceInfo != null) {
              details.addVolumeSizeGB(
                  userIntent.masterDeviceInfo.volumeSize * userIntent.masterDeviceInfo.numVolumes);
            } else {
              details.addVolumeSizeGB(
                  userIntent.deviceInfo.volumeSize * userIntent.deviceInfo.numVolumes);
            }
          }

          if (userIntent.deviceInfo.diskIops != null) {
            details.gp3FreePiops = userIntent.deviceInfo.diskIops;
          }
          if (userIntent.deviceInfo.throughput != null) {
            details.gp3FreeThroughput = userIntent.deviceInfo.throughput;
          }
        }
        if (node.cloudInfo != null && node.cloudInfo.az != null) {
          details.addAz(node.cloudInfo.az);
          // If we have resource spec use that.
          if (userIntent.tserverK8SNodeResourceSpec != null) {
            if (node.isTserver) {
              details.addMemSizeGB(userIntent.tserverK8SNodeResourceSpec.memoryGib);
              details.addNumCores(userIntent.tserverK8SNodeResourceSpec.cpuCoreCount);
            }
            // Check for master and add its resources too
            if (userIntent.masterK8SNodeResourceSpec != null) {
              if (node.isMaster) {
                details.addMemSizeGB(userIntent.masterK8SNodeResourceSpec.memoryGib);
                details.addNumCores(userIntent.masterK8SNodeResourceSpec.cpuCoreCount);
              }
            }
          } else {
            if (node.cloudInfo.instance_type != null) {
              InstanceType instanceType =
                  context.getInstanceType(
                      UUID.fromString(userIntent.provider), node.cloudInfo.instance_type);
              if (instanceType == null) {
                LOG.error(
                    "Couldn't find instance type "
                        + node.cloudInfo.instance_type
                        + " for provider "
                        + userIntent.providerType);
              } else {
                details.addMemSizeGB(instanceType.getMemSizeGB());
                details.addNumCores(instanceType.getNumCores());
              }
            }
          }
        }
      }
    }

    details.addPrice(params, context);
    return details;
  }

  @Value
  public static class Context {
    Config config;
    Map<UUID, Provider> providerMap;
    Map<ProviderAndRegion, Region> regionsMap;
    Map<InstanceTypeKey, InstanceType> instanceTypeMap;
    Map<PriceComponentKey, PriceComponent> priceComponentMap;
    boolean isCreateOrEdit;

    public Context(Config config, Universe universe) {
      this(
          config,
          Customer.get(universe.getCustomerId()),
          Collections.singletonList(universe.getUniverseDetails()));
    }

    public Context(Config config, Customer customer, UniverseDefinitionTaskParams universeParams) {
      this(config, customer, Collections.singletonList(universeParams));
    }

    public Context(
        Config config,
        Customer customer,
        UniverseDefinitionTaskParams universeParams,
        boolean isCreateOrEdit) {
      this(config, customer, Collections.singletonList(universeParams), isCreateOrEdit);
    }

    public Context(
        Config config, Customer customer, Collection<UniverseDefinitionTaskParams> universeParams) {
      this(config, customer, universeParams, false);
    }

    public Context(
        Config config,
        Customer customer,
        Collection<UniverseDefinitionTaskParams> universeParams,
        boolean isCreateOrEdit) {
      this.config = config;
      this.isCreateOrEdit = isCreateOrEdit;
      providerMap =
          Provider.getAll(customer.getUuid()).stream()
              .collect(Collectors.toMap(provider -> provider.getUuid(), Function.identity()));

      Set<InstanceTypeKey> instanceTypes =
          universeParams.stream()
              .filter(ud -> ud.nodeDetailsSet != null)
              .flatMap(
                  ud ->
                      ud.nodeDetailsSet.stream()
                          .filter(this::isNodeCounted)
                          .filter(nodeDetails -> nodeDetails.cloudInfo != null)
                          .filter(nodeDetails -> nodeDetails.cloudInfo.instance_type != null)
                          .map(
                              nodeDetails ->
                                  new InstanceTypeKey()
                                      .setProviderUuid(
                                          getProviderByPlacementUUID(ud, nodeDetails.placementUuid))
                                      .setInstanceTypeCode(nodeDetails.cloudInfo.instance_type)))
              .collect(Collectors.toSet());

      instanceTypeMap =
          InstanceType.findByKeys(instanceTypes).stream()
              .collect(Collectors.toMap(InstanceType::getIdKey, Function.identity()));

      Set<ProviderAndRegion> providersAndRegions =
          universeParams.stream()
              .filter(ud -> ud.nodeDetailsSet != null)
              .flatMap(
                  ud ->
                      ud.nodeDetailsSet.stream()
                          .filter(this::isNodeCounted)
                          .filter(nodeDetails -> nodeDetails.cloudInfo != null)
                          .filter(nodeDetails -> nodeDetails.cloudInfo.region != null)
                          .map(
                              nodeDetails ->
                                  new ProviderAndRegion(
                                      getProviderByPlacementUUID(ud, nodeDetails.placementUuid),
                                      nodeDetails.getRegion())))
              .collect(Collectors.toSet());

      regionsMap =
          Region.findByKeys(providersAndRegions).stream()
              .collect(Collectors.toMap(ProviderAndRegion::from, Function.identity()));

      priceComponentMap =
          PriceComponent.findByProvidersAndRegions(providersAndRegions).stream()
              .collect(Collectors.toMap(PriceComponent::getIdKey, Function.identity()));
    }

    private UUID getProviderByPlacementUUID(UniverseDefinitionTaskParams ud, UUID placementUuid) {
      String providerUUIDStr =
          ud.clusters.stream()
              .filter(c -> c.uuid.equals(placementUuid))
              .findFirst()
              .get()
              .userIntent
              .provider;
      return UUID.fromString(providerUUIDStr);
    }

    public Provider getProvider(UUID uuid) {
      return providerMap.get(uuid);
    }

    public Region getRegion(UUID providerUuid, String code) {
      return regionsMap.get(new ProviderAndRegion(providerUuid, code));
    }

    public InstanceType getInstanceType(UUID providerUuid, String code) {
      return instanceTypeMap.get(
          new InstanceTypeKey().setProviderUuid(providerUuid).setInstanceTypeCode(code));
    }

    public PriceComponent getPriceComponent(
        UUID providerUuid, String regionCode, String componentCode) {
      return priceComponentMap.get(
          PriceComponentKey.create(providerUuid, regionCode, componentCode));
    }

    public boolean isNodeCounted(NodeDetails nodeDetails) {
      if (isCreateOrEdit) {
        // In case we calculate cost for 'to be' universe during create or edit - count nodes,
        // which will be added and avoid counting ones, which are going to be removed.
        return (nodeDetails.isNodeRunning() || nodeDetails.state == NodeState.ToBeAdded)
            && nodeDetails.state != NodeState.ToBeRemoved;
      } else {
        return nodeDetails.isNodeRunning();
      }
    }
  }
}
