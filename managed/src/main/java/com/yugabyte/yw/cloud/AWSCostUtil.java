// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeRegionDetails;
import com.yugabyte.yw.models.InstanceType.PriceDetails;

public class AWSCostUtil {
  public static final Logger LOG = LoggerFactory.getLogger(AWSCostUtil.class);

  public static double getCostPerHour(String instanceTypeCode,
                                      String regionCode,
                                      AWSConstants.Tenancy tenancy) {
    // Get the instance type object.
    InstanceType instanceType = InstanceType.get(AWSConstants.providerCode, instanceTypeCode);
    if (instanceType == null) {
      String msg = "Region " + regionCode + " not found for instance type " + instanceTypeCode;
      LOG.error(msg);
      throw new RuntimeException(msg);
    }

    // Fetch the region info object.
    List<InstanceTypeRegionDetails> regionDetailsList =
        instanceType.instanceTypeDetails.regionCodeToDetailsMap.get(regionCode);

    InstanceTypeRegionDetails regionDetails = null;
    for (InstanceTypeRegionDetails rDetails : regionDetailsList) {
      if (rDetails.tenancy.equals(tenancy) && rDetails.operatingSystem.equals("Linux")) {
        regionDetails = rDetails;
        break;
      }
    }
    if (regionDetails == null) {
      String msg = "Tenancy " + tenancy.toString() + " not found for instance type " +
                   instanceTypeCode + ", region " + regionCode;
      LOG.error(msg);
      throw new RuntimeException(msg);
    }

    if (regionDetails.priceDetailsList.size() > 1) {
      String msg = "Found multiple price details for instance type " + instanceTypeCode +
                   ", region " + regionCode;
      LOG.error(msg);
      throw new RuntimeException(msg);
    }

    PriceDetails priceDetails = regionDetails.priceDetailsList.get(0);
    return priceDetails.pricePerUnit;
  }
}
