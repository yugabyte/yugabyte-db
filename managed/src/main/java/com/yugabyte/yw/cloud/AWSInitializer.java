// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.ApiResponse;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.InstanceType.InstanceTypeRegionDetails;
import com.yugabyte.yw.models.InstanceType.PriceDetails;
import com.yugabyte.yw.models.InstanceType.VolumeType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

import play.libs.Json;
import play.mvc.Result;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

// TODO: move pricing data fetch to ybcloud.
@Singleton
public class AWSInitializer {
  public static final Logger LOG = LoggerFactory.getLogger(AWSInitializer.class);

  public static final boolean enableVerboseLogging = false;

  @Inject
  ApiHelper apiHelper;

  // TODO: fetch the EC2 price URL from
  // 'https://pricing.us-east-1.amazonaws.com/offers/v1.0/aws/index.json'  // The AWS EC2 price url.

  public  String awsEc2PriceUrl =
      "https://pricing.us-east-1.amazonaws.com/offers/v1.0/aws/AmazonEC2/current/index.json";

  Map<String, List<PriceDetails>> ec2SkuToPriceDetails = new HashMap<String, List<PriceDetails>>();

  List<Map<String, String>> ec2AvailableInstances = new ArrayList<Map<String, String>>();

  public Result initialize(UUID customerUUID, UUID providerUUID) {
    try {
      Provider provider = Provider.get(customerUUID, providerUUID);

      LOG.info("Initializing AWS instance type and pricing info from {}", awsEc2PriceUrl);
      LOG.info("This operation may take a few minutes...");
      // Get the price Json object from the aws price url.
      JsonNode ec2PriceResponseJson = apiHelper.getRequest(awsEc2PriceUrl);

      // The products sub-document has the list of EC2 products along with the SKU, its format is:
      //    {
      //      ...
      //      "products" : {
      //        <productDetailsJson, which is a list of product details>
      //      }
      //    }
      JsonNode productDetailsListJson = ec2PriceResponseJson.get("products");

      // The "terms" or price details json object has the following format:
      //  "terms" : {
      //    "OnDemand" : {
      //      <onDemandTermsJson, which is a list of price details objects>
      //    }
      //  }
      JsonNode onDemandTermsJson = ec2PriceResponseJson.get("terms").get("OnDemand");

      parsePriceDetailsList(onDemandTermsJson);
      parseProductDetailsList(productDetailsListJson);

      // Create the instance types.
      storeInstanceTypeInfoToDB(provider.uuid);
      LOG.info("Successfully finished parsing info from {}", awsEc2PriceUrl);
    } catch (Exception e) {
      LOG.error("AWS initialize failed", e);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }

    return ApiResponse.success("AWS Initialized.");
  }

  /**
   * Parses the Json object which is a list of price details.
   *
   * @param onDemandTermsJson the 'terms' > 'OnDemand' JSON object.
   */
  private void parsePriceDetailsList(JsonNode onDemandTermsJson) {
    Iterator<JsonNode> priceDetailObjsIter = onDemandTermsJson.elements();

    // Iterate over the price detail objects. Each price detail object has the format:
    //
    //      "DQ578CGN99KG6ECF" : {
    //        "DQ578CGN99KG6ECF.JRTCKXETXF" : {
    //          "offerTermCode" : "JRTCKXETXF",
    //          "sku" : "DQ578CGN99KG6ECF",
    //          "effectiveDate" : "2016-08-01T00:00:00Z",
    //          "priceDimensions" : {
    //            "DQ578CGN99KG6ECF.JRTCKXETXF.6YS6EN2CT7" : {
    //              "rateCode" : "DQ578CGN99KG6ECF.JRTCKXETXF.6YS6EN2CT7",
    //              "description" : "$4.931 per On Demand Windows hs1.8xlarge Instance Hour",
    //              "beginRange" : "0",
    //              "endRange" : "Inf",
    //              "unit" : "Hrs",
    //              "pricePerUnit" : {
    //                "USD" : "4.9310000000"
    //              },
    //              "appliesTo" : [ ]
    //            }
    //          },
    //          "termAttributes" : { }
    //        }
    //      }
    while (priceDetailObjsIter.hasNext()) {
      JsonNode priceDetailObj = priceDetailObjsIter.next();
      Iterator<JsonNode> singleTermIter = priceDetailObj.elements();

      int numSingleTermEntries = 0;
      while (singleTermIter.hasNext()) {
        // We expect only one entry. If there are multiple, take the first, skip the rest.
        numSingleTermEntries++;
        if (numSingleTermEntries > 1) {
          String msg = "Multiple entries encountered for: " + priceDetailObj.toString();
          LOG.error(msg);
          break;
        }
        JsonNode singleTerm = singleTermIter.next();

        // Get the hash key for this price term object.
        String sku = singleTerm.findValue("sku").textValue();

        Iterator<JsonNode> priceDimensionsIter = singleTerm.findValue("priceDimensions").elements();
        while (priceDimensionsIter.hasNext()) {
          // Create the price details object along with the details we have so far.
          PriceDetails priceDetails = new PriceDetails();
          if (singleTerm.has("effectiveDate")) {
            priceDetails.effectiveDate = singleTerm.findValue("effectiveDate").textValue();
          }

          JsonNode priceDimension = priceDimensionsIter.next();
          priceDetails.description = priceDimension.findValue("description").textValue();
          priceDetails.beginRange = priceDimension.findValue("beginRange").textValue();
          priceDetails.endRange = priceDimension.findValue("endRange").textValue();
          Iterator<Entry<String, JsonNode>> iter =
              priceDimension.findValue("pricePerUnit").fields();
          String priceInUSDStr = null;
          while (iter.hasNext()) {
            Entry<String, JsonNode> entry = iter.next();
            String currency = entry.getKey();
            if (!currency.equals(PriceDetails.Currency.USD.toString())) {
              continue;
            }
            priceInUSDStr = entry.getValue().textValue();
            // The string will have double quotes before and after it, remove those.
            priceInUSDStr = priceInUSDStr.substring(1, priceInUSDStr.length() - 1);
          }
          if (priceInUSDStr == null) {
            String msg = "Price not found, maybe units are not USD";
            LOG.error(msg);
            throw new UnsupportedOperationException(msg);
          }
          priceDetails.currency = PriceDetails.Currency.USD;
          priceDetails.pricePerUnit = Double.parseDouble(priceInUSDStr);
          if (enableVerboseLogging) {
            LOG.info("Parsed price entry sku={}, effectiveDate={}, description={}, priceInUSD={}",
                     sku, priceDetails.effectiveDate, priceDetails.description,
                     priceDetails.pricePerUnit);
          }

          // Add the price details to the map.
          List<PriceDetails> priceDetailsList = ec2SkuToPriceDetails.get(sku);
          if (priceDetailsList == null) {
            priceDetailsList = new ArrayList<PriceDetails>();
          }
          priceDetailsList.add(priceDetails);
          ec2SkuToPriceDetails.put(sku, priceDetailsList);
        }
      }
    }
  }

  private void parseProductDetailsList(JsonNode productDetailsListJson) {
    LOG.info("Parsing product details list");
    Iterator<JsonNode> productDetailsListIter = productDetailsListJson.elements();

    // Iterate over the product details objects, each of which has the format:
    //    "DQ578CGN99KG6ECF" : {
    //      "sku" : "DQ578CGN99KG6ECF",
    //      "productFamily" : "Compute Instance",
    //      "attributes" : {
    //        "servicecode" : "AmazonEC2",
    //        "location" : "US East (N. Virginia)",
    //        "locationType" : "AWS Region",
    //        "instanceType" : "hs1.8xlarge",
    //        "currentGeneration" : "No",
    //        "instanceFamily" : "Storage optimized",
    //        "vcpu" : "17",
    //        "physicalProcessor" : "Intel Xeon E5-2650",
    //        "clockSpeed" : "2 GHz",
    //        "memory" : "117 GiB",
    //        "storage" : "24 x 2000",
    //        "networkPerformance" : "10 Gigabit",
    //        "processorArchitecture" : "64-bit",
    //        "tenancy" : "Shared",
    //        "operatingSystem" : "Windows",
    //        "licenseModel" : "License Included",
    //        "usagetype" : "BoxUsage:hs1.8xlarge",
    //        "operation" : "RunInstances:0002",
    //        "preInstalledSw" : "NA"
    //      }
    //    }
    while (productDetailsListIter.hasNext()) {
      JsonNode productDetailsJson = productDetailsListIter.next();

      Map<String, String> productAttrs = extractAllAttributes(productDetailsJson);

      boolean include = true;

      // Make sure this is a compute instance.
      include &= matches(productAttrs, "productFamily", FilterOp.Equals, "Compute Instance");
      // The service code should be 'AmazonEC2'.
      include &= matches(productAttrs, "servicecode", FilterOp.Equals, "AmazonEC2");
      // Filter by the OS we support.
      include &= (matches(productAttrs, "operatingSystem", FilterOp.Equals, "Linux") ||
                  matches(productAttrs, "operatingSystem", FilterOp.Equals, "RHEL") ||
                  matches(productAttrs, "operatingSystem", FilterOp.Equals, "NA"));
      // Pick the supported license models.
      include &= (matches(productAttrs, "licenseModel", FilterOp.Equals, "No License required") ||
                  matches(productAttrs, "licenseModel", FilterOp.Equals, "NA"));
      // Pick the valid disk drive types.
      include &= (matches(productAttrs, "storage", FilterOp.Contains, "SSD") ||
                  matches(productAttrs, "storage", FilterOp.Contains, "EBS"));
      // Make sure it is current generation.
      include &= matches(productAttrs, "currentGeneration", FilterOp.Equals, "Yes");
      // Make sure tenancy is shared
      include &= matches(productAttrs, "tenancy", FilterOp.Equals, "Shared");

      if (!include) {
        if (enableVerboseLogging) {
          LOG.info("Skipping product");
        }
        continue;
      }

      if (enableVerboseLogging) {
        LOG.info("Found matching product with sku={}, instanceType={}",
                 productAttrs.get("sku"), productAttrs.get("instanceType"));
      }

      ec2AvailableInstances.add(productAttrs);
    }
  }

  /**
   * Store information about the various instance types to the database. Uses UPSERT semantics if
   * the row for the instance type already exists.
   */
  private void storeInstanceTypeInfoToDB(UUID awsUUID) {
    LOG.info("Storing AWS instance type and pricing info in Yugaware DB");
    // First reset all the JSON details of all entries in the table, as we are about to refresh it.
    InstanceType.resetAllInstanceTypeDetails();

    for (Map<String, String> productAttrs : ec2AvailableInstances) {
      String providerCode = "aws";

      // Get the instance type.
      String instanceTypeCode = productAttrs.get("instanceType");

      // The number of cores is the number of vcpu's.
      if (productAttrs.get("vcpu") == null) {
        String msg = "Error parsing sku=" + productAttrs.get("sku") + ", num vcpu missing";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }
      Integer numCores = Integer.parseInt(productAttrs.get("vcpu"));

      // Parse the memory size.
      String memSizeStrGB = productAttrs.get("memory").replaceAll("(?i) gib", "");
      Double memSizeGB = Double.parseDouble(memSizeStrGB);

      Integer volumeCount;
      Integer volumeSizeGB;
      VolumeType volumeType;
      // Parse the local instance store details. Format of the raw data is either "1 x 800 SSD" or
      // "12 x 2000 HDD".
      String[] parts = productAttrs.get("storage").split(" ");
      if (parts.length < 4) {
        if (!productAttrs.get("storage").equals("EBS only")) {
          String msg = "Volume type not specified in product sku=" + productAttrs.get("sku") +
              ", storage={" + productAttrs.get("storage") + "}";
          LOG.error(msg);
          throw new UnsupportedOperationException(msg);
        } else {
          // TODO: hardcode me not?
          volumeCount = 2;
          volumeSizeGB = 250;
          volumeType = VolumeType.EBS;
        }
      } else if (instanceTypeCode.startsWith("i3")) {
        // TODO: remove this once aws pricing api is fixed
        // TODO: see discussion: https://forums.aws.amazon.com/thread.jspa?messageID=783507
        String[] instanceTypeParts = instanceTypeCode.split("\\.");
        volumeType = VolumeType.SSD;
        switch (instanceTypeParts[1]) {
          case "large":
            volumeCount = 1;
            volumeSizeGB = 475;
            break;
          case "xlarge":
            volumeCount = 1;
            volumeSizeGB = 950;
            break;
          case "2xlarge":
            volumeCount = 1;
            volumeSizeGB = 1900;
            break;
          case "4xlarge":
            volumeCount = 2;
            volumeSizeGB = 1900;
            break;
          case "8xlarge":
            volumeCount = 4;
            volumeSizeGB = 1900;
            break;
          case "16xlarge":
          default:
            volumeCount = 8;
            volumeSizeGB = 1900;
            break;
        }
      } else {
        volumeCount = Integer.parseInt(parts[0]);
        volumeSizeGB = Integer.parseInt(parts[2]);
        volumeType = VolumeType.valueOf(parts[3]);
      }

      // Fill up all the per-region details.
      String regionName = productAttrs.get("location");
      Region region =
          Region.find.where().eq("provider_uuid", awsUUID).eq("name", regionName).findUnique();
      if (region == null) {
        LOG.error("Region " + regionName + " not found. Skipping.");
        continue;
      }
      String regionCode = region.code;
      InstanceTypeRegionDetails regionDetails = new InstanceTypeRegionDetails();
      regionDetails.operatingSystem = productAttrs.get("operatingSystem");
      regionDetails.productFamily = productAttrs.get("productFamily");
      regionDetails.servicecode = productAttrs.get("servicecode");
      regionDetails.tenancy = PublicCloudConstants.Tenancy.valueOf(productAttrs.get("tenancy"));

      // Fill up the price details, using the sku to lookup.
      regionDetails.priceDetailsList = ec2SkuToPriceDetails.get(productAttrs.get("sku"));
      if (regionDetails.priceDetailsList == null || regionDetails.priceDetailsList.isEmpty()) {
        String msg = "Failed to get price details for sku=" + productAttrs.get("sku");
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      if (enableVerboseLogging) {
        LOG.info("Instance type entry ({}, {}): {} cores, {} GB RAM, {} x {} GB {} in region {}",
                 providerCode, instanceTypeCode, numCores, memSizeGB,
                 volumeCount, volumeSizeGB, volumeType, regionCode);
      }

      // Create the instance type model. If one already exists, overwrite it.
      InstanceType instanceType = InstanceType.get(providerCode, instanceTypeCode);
      if (instanceType == null) {
        instanceType = new InstanceType();
      }
      if (instanceType.instanceTypeDetails == null) {
        instanceType.instanceTypeDetails = new InstanceTypeDetails();
      }
      List<InstanceType.VolumeDetails> volumeDetailsList =
          instanceType.instanceTypeDetails.volumeDetailsList;
      if (volumeDetailsList == null || volumeDetailsList.isEmpty()) {
        for (int i = 0; i < volumeCount; ++i) {
          InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
          volumeDetails.volumeSizeGB = volumeSizeGB;
          volumeDetails.volumeType = volumeType;
          instanceType.instanceTypeDetails.volumeDetailsList.add(volumeDetails);
        }
        instanceType.instanceTypeDetails.setDefaultMountPaths();
      }
      List<InstanceTypeRegionDetails> detailsList =
          instanceType.instanceTypeDetails.regionCodeToDetailsMap.get(regionCode);
      if (detailsList == null) {
        detailsList = new ArrayList<InstanceTypeRegionDetails>();
      }
      detailsList.add(regionDetails);

      // Fill up the instance type details.
      instanceType.instanceTypeDetails.regionCodeToDetailsMap.put(regionCode, detailsList);

      // Update the object.
      if (enableVerboseLogging) {
        LOG.debug("Saving {} ({} cores, {}GB) with details {}",
            instanceType.idKey.toString(), instanceType.numCores, instanceType.memSizeGB,
            Json.stringify(Json.toJson(instanceType.instanceTypeDetails)));
      }
      InstanceType.upsert(providerCode,
                          instanceTypeCode,
                          numCores,
                          memSizeGB,
                          instanceType.instanceTypeDetails);
    }
  }

  enum FilterOp {
    Equals,
    Contains,
  }

  private boolean matches(Map<String, String> objAttrs, String name, FilterOp op, String value) {
    if (op == FilterOp.Equals) {
      return value.equals(objAttrs.get(name));
    } else if (op == FilterOp.Contains) {
      if (!objAttrs.containsKey(name)) {
        return false;
      }
      return objAttrs.get(name).contains(value);
    }
    return false;
  }

  private Map<String, String> extractAllAttributes(JsonNode productDetailsJson) {
    Map<String, String> productAttrs = new HashMap<String, String>();
    StringBuilder sb = new StringBuilder();
    sb.append("Product: sku=" + productDetailsJson.get("sku").textValue());
    sb.append(", productFamily=" + productDetailsJson.get("productFamily").textValue());
    productAttrs.put("sku", productDetailsJson.get("sku").textValue());
    productAttrs.put("productFamily", productDetailsJson.get("productFamily").textValue());
    // Iterate over all the attributes.
    Iterator<Map.Entry<String,JsonNode>> iter = productDetailsJson.get("attributes").fields();
    while (iter.hasNext()) {
      Map.Entry<String,JsonNode> entry = iter.next();
      productAttrs.put(entry.getKey(), entry.getValue().textValue());
      sb.append(", " + entry.getKey() + "=" + entry.getValue().textValue());
    }
    if (enableVerboseLogging) {
      LOG.info(sb.toString());
    }
    return productAttrs;
  }
}
