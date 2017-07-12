// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import com.yugabyte.yw.models.helpers.DeviceInfo;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.cloud.PublicCloudConstants.Tenancy;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.InstanceType.PriceDetails;

public class ResourceUtil {
  public static final Logger LOG = LoggerFactory.getLogger(ResourceUtil.class);
  
  private static Tenancy getTenancy(InstanceType instanceType, 
  																	CloudType cloudType, 
  																	String regionCode) {
  	switch (cloudType) {
  		case gcp: 
  			List<InstanceType.InstanceTypeRegionDetails> instanceTypeRegionDetails = 
					instanceType.instanceTypeDetails.regionCodeToDetailsMap.get(regionCode);
  			return instanceTypeRegionDetails.get(0).tenancy;
  		default:
  			// TODO: Deal with other clouds casewise. Shared might not work except for AWS/GCP.
  			return Tenancy.Shared;
  	}
  }
  
  public static void mergeResourceDetails(DeviceInfo deviceInfo, 
  																				CloudType cloudType, 
  																				String instanceTypeCode, 
  																				String azCode,
  																				String regionCode, 
  																				UniverseResourceDetails universeResourceDetails) {
    // Get the instance type object.
    InstanceType instanceType = InstanceType.get(cloudType.toString(), instanceTypeCode);
    if (instanceType == null) {
      String msg = "Region " + regionCode + " not found for instance type " + instanceTypeCode;
      LOG.error(msg);
      throw new RuntimeException(msg);
    }
    Tenancy tenancy = getTenancy(instanceType, cloudType, regionCode);
    PriceDetails priceDetails = instanceType.getPriceDetails(regionCode, tenancy);
    universeResourceDetails.addCostPerHour(priceDetails.pricePerUnit);
    universeResourceDetails.addvolumeSizeGB(deviceInfo.volumeSize * deviceInfo.numVolumes);
    universeResourceDetails.addmemSizeGB(instanceType.memSizeGB);
    universeResourceDetails.addAz(azCode);
    universeResourceDetails.addNumCores(instanceType.numCores);
    universeResourceDetails.addVolumeCount(deviceInfo.numVolumes);
  }
}
