// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.cloud;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;

import org.apache.http.HttpEntity;
import org.apache.http.HttpResponse;
import org.apache.http.client.ClientProtocolException;
import org.apache.http.client.ResponseHandler;
import org.apache.http.client.methods.HttpGet;
import org.apache.http.impl.client.CloseableHttpClient;
import org.apache.http.impl.client.HttpClientBuilder;
import org.apache.http.util.EntityUtils;
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
import play.mvc.Controller;
import play.mvc.Result;

public class AWSInitializer extends Controller {
  public static final Logger LOG = LoggerFactory.getLogger(AWSInitializer.class);

  @Inject
  ApiHelper apiHelper;

  // TODO: fetch the EC2 price URL from
  // 'https://pricing.us-east-1.amazonaws.com/offers/v1.0/aws/index.json'  // The AWS EC2 price url.

  private String awsEc2PriceUrl =
      "https://pricing.us-east-1.amazonaws.com/offers/v1.0/aws/AmazonEC2/current/index.json";

  Map<String, List<PriceDetails>> ec2SkuToPriceDetails = new HashMap<String, List<PriceDetails>>();

  List<Map<String, String>> ec2AvailableInstances = new ArrayList<Map<String, String>>();

  public Result run() {
    try {
      // Get the price Json object from the aws price url.
      JsonNode ec2PriceResponseJson = doGetRequestAsJson(awsEc2PriceUrl);

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
      storeInstanceTypeInfoToDB();
    } catch (Exception e) {
      LOG.error("AWS initialize failed", e);
      return internalServerError(e.getMessage());
    }

    return ok();
  }

  public static JsonNode doGetRequestAsJson(String url) {
    LOG.info("Fetching response from url {}", url);
    CloseableHttpClient httpclient = HttpClientBuilder.create().build();
    String responseBody = null;
    try {
      // Fetch data from the AWS EC2 url.
      HttpGet httpget = new HttpGet(url);

      // Create a custom response handler.
      ResponseHandler<String> responseHandler = new ResponseHandler<String>() {
        @Override
        public String handleResponse(final HttpResponse response)
            throws ClientProtocolException, IOException {
          int status = response.getStatusLine().getStatusCode();
          if (status >= 200 && status < 300) {
            HttpEntity entity = response.getEntity();
            return entity != null ? EntityUtils.toString(entity) : null;
          } else {
            throw new ClientProtocolException("Unexpected response status: " + status);
          }
        }
      };
      responseBody = httpclient.execute(httpget, responseHandler);
    } catch (IOException e) {
      LOG.error("Error fetching GET request from " + url, e);
      throw new RuntimeException(e);
    } finally {
      try {
        httpclient.close();
      } catch (IOException e) {
        LOG.error("Error closing the http client", e);
      }
    }
    LOG.info("Got {} bytes of response, parsing Json object", responseBody.length());
    // Parse the response into a Json object.
    JsonNode jsonObject = Json.parse(responseBody);
    return jsonObject;
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
        // We expect only one entry in this list. If there are multiple, error out as we have not
        // implemented that logic yet.
        numSingleTermEntries++;
        if (numSingleTermEntries > 1) {
          String msg = "Not able to parse price detail object: " + priceDetailObj.toString();
          LOG.error(msg);
          throw new UnsupportedOperationException(msg);
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
          LOG.info("Parsed price entry sku={}, effectiveDate={}, description={}, priceInUSD={}",
                   sku, priceDetails.effectiveDate, priceDetails.description,
                   priceDetails.pricePerUnit);

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
      include &= matches(productAttrs, "storage", FilterOp.Contains, "SSD");
      // Make sure it is current generation.
      include &= matches(productAttrs, "currentGeneration", FilterOp.Equals, "Yes");

      if (!include) {
        LOG.info("Skipping product");
        continue;
      }

      LOG.info("Found matching product with sku={}, instanceType={}",
               productAttrs.get("sku"), productAttrs.get("instanceType"));

      ec2AvailableInstances.add(productAttrs);
    }
  }

  /**
   * Store information about the various instance types to the database. Uses UPSERT semantics if
   * the row for the instance type already exists.
   */
  private void storeInstanceTypeInfoToDB() {
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
      String memSizeStrGB = productAttrs.get("memory").replace(" GiB", "");
      Double memSizeGB = Double.parseDouble(memSizeStrGB);

      // Parse the local instance store details. Format of the raw data is either "1 x 800 SSD" or
      // "12 x 2000 HDD".
      String[] parts = productAttrs.get("storage").split(" ");
      if (parts.length < 4) {
        String msg = "Volume type not specified in product sku=" + productAttrs.get("sku") +
                     ", storage={" + productAttrs.get("storage") + "}";
        LOG.error(msg);
        throw new UnsupportedOperationException(msg);
      }
      Integer volumeCount = Integer.parseInt(parts[0]);
      Integer volumeSizeGB = Integer.parseInt(parts[2]);
      VolumeType volumeType = VolumeType.valueOf(parts[3]);

      // Fill up all the per-region details.
      UUID awsUUID = Provider.get("Amazon").uuid;
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
      regionDetails.tenancy = AWSConstants.Tenancy.valueOf(productAttrs.get("tenancy"));

      // Fill up the price details, using the sku to lookup.
      regionDetails.priceDetailsList = ec2SkuToPriceDetails.get(productAttrs.get("sku"));
      if (regionDetails.priceDetailsList == null || regionDetails.priceDetailsList.isEmpty()) {
        String msg = "Failed to get price details for sku=" + productAttrs.get("sku");
        LOG.error(msg);
        throw new RuntimeException(msg);
      }

      LOG.info("Instance type entry ({}, {}): {} cores, {} GB RAM, {} x {} GB {} in region {}",
               providerCode, instanceTypeCode, numCores, memSizeGB,
               volumeCount, volumeSizeGB, volumeType, regionCode);

      // Create the instance type model.
      InstanceType instanceType = InstanceType.get(providerCode, instanceTypeCode);
      if (instanceType == null) {
        instanceType = new InstanceType();
      }
      if (instanceType.instanceTypeDetails == null) {
        instanceType.instanceTypeDetails = new InstanceTypeDetails();
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
      InstanceType.upsert(providerCode,
                          instanceTypeCode,
                          numCores,
                          memSizeGB,
                          volumeCount,
                          volumeSizeGB,
                          volumeType,
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
    LOG.info(sb.toString());
    return productAttrs;
  }
}
