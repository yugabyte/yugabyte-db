// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import java.util.Calendar;
import java.util.List;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeRegionDetails;
import com.yugabyte.yw.models.InstanceType.PriceDetails;

public class AWSResourceUtil {
  public static final Logger LOG = LoggerFactory.getLogger(AWSResourceUtil.class);

  public static void mergeResourceDetails(DeviceInfo deviceInfo,
                                          String instanceTypeCode, String azCode,
                                          String regionCode, AWSConstants.Tenancy tenancy,
                                          UniverseResourceDetails universeResourceDetails) {
    // Get the instance type object.
    InstanceType instanceType = InstanceType.get(AWSConstants.providerCode, instanceTypeCode);
    if (instanceType == null) {
      String msg = "Region " + regionCode + " not found for instance type " + instanceTypeCode;
      LOG.error(msg);
      throw new RuntimeException(msg);
    }
    PriceDetails priceDetails = instanceType.getPriceDetails(regionCode, tenancy, azCode);
    universeResourceDetails.addCostPerHour(priceDetails.pricePerUnit);
    universeResourceDetails.addvolumeSizeGB(deviceInfo.volumeSize * deviceInfo.numVolumes);
    universeResourceDetails.addmemSizeGB(instanceType.memSizeGB);
    universeResourceDetails.addAz(azCode);
    universeResourceDetails.addNumCores(instanceType.numCores);
    universeResourceDetails.addVolumeCount(deviceInfo.numVolumes);
  }
}
