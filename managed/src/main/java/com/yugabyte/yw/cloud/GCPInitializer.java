// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.cloud;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.InstanceType.InstanceTypeRegionDetails;
import com.yugabyte.yw.models.InstanceType.PriceDetails;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.common.CloudQueryHelper;

import play.mvc.Result;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

@Singleton
public class GCPInitializer {
  public static final Logger LOG = LoggerFactory.getLogger(GCPInitializer.class);

  public static final boolean enableVerboseLogging = false;

  @Inject
  ApiHelper apiHelper;
  
  @Inject
  CloudQueryHelper cloudQueryHelper;
  
  public Map<String, List<InstanceTypeRegionDetails>> getRegionCodeToDetailsMap(JsonNode instanceTypeToDetailsMap) {
  	Map<String, List<InstanceTypeRegionDetails>> regionCodeToDetailsMap = new HashMap<String, List<InstanceTypeRegionDetails>>();
  	
  	JsonNode regionToPriceMap = instanceTypeToDetailsMap.get("prices");
  	
  	Iterator<String> regionCodeItr = regionToPriceMap.fieldNames();
  	while(regionCodeItr.hasNext()) {
  		String regionCode = regionCodeItr.next();
  		JsonNode osToPriceList = regionToPriceMap.get(regionCode);
  		List<InstanceTypeRegionDetails> detailsMapList = new ArrayList<InstanceTypeRegionDetails>();
  		for (JsonNode osJson: osToPriceList) {
  			InstanceTypeRegionDetails detailsMap = new InstanceTypeRegionDetails();
    		
    		detailsMap.servicecode = "GCP Compute Engine";
    		detailsMap.productFamily = "VM Instance";
    		
    		if (instanceTypeToDetailsMap.get("isShared").asBoolean()) {
    			detailsMap.tenancy = PublicCloudConstants.Tenancy.Shared;
    		} else {
    			detailsMap.tenancy = PublicCloudConstants.Tenancy.Dedicated;
    		}
    		
    		detailsMap.operatingSystem = osJson.get("os").asText();
    		
    		List<PriceDetails> priceDetailsList = new ArrayList<PriceDetails>(); 
    		
    		PriceDetails priceDetailsMap = new PriceDetails();
    		priceDetailsMap.unit = PriceDetails.Unit.Hours;
    		priceDetailsMap.pricePerUnit = osJson.get("price").asDouble();
    		priceDetailsMap.currency = PriceDetails.Currency.USD;
    		priceDetailsMap.beginRange = "0";
    		priceDetailsMap.endRange = "Inf";
    		priceDetailsMap.effectiveDate = "2017-06-01T00:00:00Z";
    		priceDetailsMap.description = instanceTypeToDetailsMap.get("description").asText();
    		
    		priceDetailsList.add(priceDetailsMap);
    		
    		detailsMap.priceDetailsList = priceDetailsList;
    		detailsMapList.add(detailsMap);
  		}
  		regionCodeToDetailsMap.put(regionCode, detailsMapList);
  	}
  	return regionCodeToDetailsMap;
  }

  public Result initialize(UUID customerUUID, UUID providerUUID) {
  	try {
  		Provider provider = Provider.get(providerUUID);
  		List<Region> regionList = Region.find.where().eq("provider_uuid", providerUUID).findList();
  		JsonNode instanceTypes = cloudQueryHelper.getInstanceTypes(Common.CloudType.valueOf(provider.code), regionList);
  		Iterator<String> itr = instanceTypes.fieldNames();
  		while(itr.hasNext()) {
  			String instanceTypeCode = itr.next();
				JsonNode instanceTypeToDetailsMap = instanceTypes.get(instanceTypeCode);
				Integer numCores = instanceTypeToDetailsMap.get("numCores").asInt();
				Double memSizeGb = instanceTypeToDetailsMap.get("memSizeGb").asDouble();
				
				InstanceTypeDetails instanceTypeDetails = InstanceTypeDetails.getInstanceTypeDetails(
																										getRegionCodeToDetailsMap(instanceTypeToDetailsMap));
				
				InstanceType.upsert(provider.code, 
														instanceTypeCode, 
														numCores, 
														memSizeGb, 
														instanceTypeDetails);
			}
  	} catch (Exception e) {
  		LOG.error("GCP Initialize failed", e);
  		return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
  	}
  	
  	return ApiResponse.success("GCP Initialized");
  }
}
