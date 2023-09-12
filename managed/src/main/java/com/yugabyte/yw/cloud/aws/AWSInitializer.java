/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.cloud.aws;

import static com.yugabyte.yw.cloud.PublicCloudConstants.GP2_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GP3_THROUGHPUT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GROUP_EBS_IOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.GROUP_EBS_THROUGHPUT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_PIOPS;
import static com.yugabyte.yw.cloud.PublicCloudConstants.IO1_SIZE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.PRODUCT_FAMILY_COMPUTE_INSTANCE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.PRODUCT_FAMILY_PROVISIONED_THROUGHPUT;
import static com.yugabyte.yw.cloud.PublicCloudConstants.PRODUCT_FAMILY_STORAGE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.PRODUCT_FAMILY_SYSTEM_OPERATION;
import static com.yugabyte.yw.cloud.PublicCloudConstants.VOLUME_API_GENERAL_PURPOSE;
import static com.yugabyte.yw.cloud.PublicCloudConstants.VOLUME_API_NAME_GP2;
import static com.yugabyte.yw.cloud.PublicCloudConstants.VOLUME_API_NAME_GP3;
import static com.yugabyte.yw.cloud.PublicCloudConstants.VOLUME_API_NAME_IO1;
import static com.yugabyte.yw.cloud.PublicCloudConstants.VOLUME_TYPE_PROVISIONED_IOPS;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.AbstractInitializer;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.InstanceType.VolumeType;
import com.yugabyte.yw.models.PriceComponent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import play.Environment;
import play.libs.Json;

// TODO: move pricing data fetch to ybcloud.
@Singleton
public class AWSInitializer extends AbstractInitializer {

  private static final boolean enableVerboseLogging = false;

  @Inject Environment environment;

  @Inject Config config;

  @Inject RuntimeConfGetter runtimeConfGetter;

  /**
   * Entry point to initialize AWS. This will create the various InstanceTypes and their
   * corresponding PriceComponents per Region for AWS as well as the EBS pricing info.
   *
   * @param customerUUID UUID of the Customer.
   * @param providerUUID UUID of the Customer's configured AWS.
   */
  @Override
  public void initialize(UUID customerUUID, UUID providerUUID) {
    Provider provider = Provider.get(customerUUID, providerUUID);
    InitializationContext context = new InitializationContext(provider);

    LOG.info("Initializing AWS instance type and pricing info.");
    LOG.info("This operation may take a few minutes...");
    // Get the price Json object stored locally at conf/aws_pricing.
    for (Region region : provider.getRegions()) {
      JsonNode regionJson = null;

      String pricingFileName = "aws_pricing/" + region.getCode() + ".tar.gz";
      try (InputStream pricingStream = environment.resourceAsStream(pricingFileName);
          GzipCompressorInputStream gzipStream = new GzipCompressorInputStream(pricingStream);
          TarArchiveInputStream regionStream = new TarArchiveInputStream(gzipStream)) {
        TarArchiveEntry currentEntry;
        boolean pricingFileFound = false;
        while ((currentEntry = regionStream.getNextTarEntry()) != null) {
          if (currentEntry.getName().equals(region.getCode())) {
            pricingFileFound = true;
            break;
          } else {
            LOG.warn("Unexpected file in pricing archive {}", currentEntry.getName());
          }
        }
        if (!pricingFileFound) {
          LOG.error("Failed to get region pricing file from {}", pricingFileName);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to get region pricing file");
        }
        ObjectMapper mapper = new ObjectMapper();
        regionJson = mapper.readTree(regionStream);
      } catch (IOException e) {
        LOG.error("Failed to parse region metadata from region {}", region.getCode());
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Failed to parse region metadata from region "
                + region.getCode()
                + ". "
                + e.getMessage());
      }

      // The products sub-document has the list of EC2 products along with the SKU, its format is:
      //    {
      //      ...
      //      "products" : {
      //        <productDetailsJson, which is a list of product details>
      //      }
      //    }
      JsonNode productDetailsListJson = regionJson.get("products");

      // The "terms" or price details json object has the following format:
      //  "terms" : {
      //    "OnDemand" : {
      //      <onDemandJson, which is a list of price details objects>
      //    }
      //  }
      JsonNode onDemandJson = regionJson.get("terms").get("OnDemand");

      storeEBSPriceComponents(context, productDetailsListJson, onDemandJson);
      storeInstancePriceComponents(context, productDetailsListJson, onDemandJson, region);
      parseProductDetailsList(context, productDetailsListJson, region);

      // Create the instance types.
      storeInstanceTypeInfoToDB(context);
      LOG.info("Successfully stored pricing info for region {}", region.getCode());
    }
    LOG.info("Successfully finished parsing pricing info.");
  }

  /**
   * This will store the PriceComponents corresponding to EBS. Example IO1 size json blobs:
   * "KA7RG53ZHMXMZFAF" : { "KA7RG53ZHMXMZFAF.JRTCKXETXF" : { "offerTermCode" : "JRTCKXETXF", "sku"
   * : "KA7RG53ZHMXMZFAF", "effectiveDate" : "2017-06-01T00:00:00Z", "priceDimensions" : {
   * "KA7RG53ZHMXMZFAF.JRTCKXETXF.6YS6EN2CT7" : { "rateCode" :
   * "KA7RG53ZHMXMZFAF.JRTCKXETXF.6YS6EN2CT7", "description" : "$0.145 per GB-month of Provisioned
   * IOPS SSD (io1) provisioned storage - EU (London)", "beginRange" : "0", "endRange" : "Inf",
   * "unit" : "GB-Mo", "pricePerUnit" : { "USD" : "0.1450000000" }, "appliesTo" : [ ] } },
   * "termAttributes" : { } } }, "KA7RG53ZHMXMZFAF" : { "sku" : "KA7RG53ZHMXMZFAF", "productFamily"
   * : "Storage", "attributes" : { "servicecode" : "AmazonEC2", "location" : "EU (London)",
   * "locationType" : "AWS Region", "storageMedia" : "SSD-backed", "volumeType" : "Provisioned
   * IOPS", "maxVolumeSize" : "16 TiB", "maxIopsvolume" : "20000", "maxThroughputvolume" : "320
   * MB/sec", "usagetype" : "EUW2-EBS:VolumeUsage.piops", "operation" : "" } },
   *
   * @param productDetailsListJson Products sub-document with list of EC2 products along with SKU.
   * @param onDemandJson Price details json object.
   */
  private void storeEBSPriceComponents(
      InitializationContext context, JsonNode productDetailsListJson, JsonNode onDemandJson) {
    LOG.info("Parsing product details list to store pricing info");
    for (JsonNode productDetailsJson : productDetailsListJson) {
      String sku = productDetailsJson.get("sku").textValue();
      JsonNode attributesJson = productDetailsJson.get("attributes");
      JsonNode regionJson = attributesJson.get("location");
      if (regionJson == null) {
        if (enableVerboseLogging) {
          LOG.error("No region available for product SKU " + sku + ". Skipping.");
        }
        continue;
      }
      Region region =
          Region.find
              .query()
              .where()
              .eq("provider_uuid", context.getProvider().getUuid())
              .eq("name", regionJson.textValue())
              .findOne();
      if (region == null) {
        if (enableVerboseLogging) {
          LOG.error("No region " + regionJson.textValue() + " available");
        }
        continue;
      }
      if (productDetailsJson.get("productFamily") != null) {
        switch (productDetailsJson.get("productFamily").textValue()) {
          case PRODUCT_FAMILY_STORAGE:
            JsonNode volumeType = attributesJson.get("volumeType");
            if (VOLUME_TYPE_PROVISIONED_IOPS.equals(volumeType.textValue())) {
              storeEBSPriceComponent(context, sku, IO1_SIZE, region, onDemandJson);
            } else if (VOLUME_API_GENERAL_PURPOSE.equals(volumeType.textValue())) {
              JsonNode volumeApiName = attributesJson.get("volumeApiName");
              if (VOLUME_API_NAME_GP2.equals(volumeApiName.textValue())) {
                storeEBSPriceComponent(context, sku, GP2_SIZE, region, onDemandJson);
              } else if (VOLUME_API_NAME_GP3.equals(volumeApiName.textValue())) {
                storeEBSPriceComponent(context, sku, GP3_SIZE, region, onDemandJson);
              }
            }
            break;
          case PRODUCT_FAMILY_SYSTEM_OPERATION:
            if (GROUP_EBS_IOPS.equals(attributesJson.get("group").textValue())) {
              JsonNode volumeApiName = attributesJson.get("volumeApiName");
              if (VOLUME_API_NAME_IO1.equals(volumeApiName.textValue())) {
                storeEBSPriceComponent(context, sku, IO1_PIOPS, region, onDemandJson);
              } else if (VOLUME_API_NAME_GP3.equals(volumeApiName.textValue())) {
                storeEBSPriceComponent(context, sku, GP3_PIOPS, region, onDemandJson);
              }
            }
            break;
          case PRODUCT_FAMILY_PROVISIONED_THROUGHPUT:
            if (GROUP_EBS_THROUGHPUT.equals(attributesJson.get("group").textValue())) {
              JsonNode volumeApiName = attributesJson.get("volumeApiName");
              if (VOLUME_API_NAME_GP3.equals(volumeApiName.textValue())) {
                storeEBSPriceComponent(context, sku, GP3_THROUGHPUT, region, onDemandJson);
              }
            }
            break;
          default:
            break;
        }
      }
    }
  }

  /**
   * Given info about a single EBS item (size/piops) in a specific region, store its PriceComponent.
   *
   * @param sku SKU of the EBS item in its region.
   * @param componentCode Code for the EBS item (e.g. io1.size).
   * @param region The region the EBS item is in (e.g. us-west2).
   * @param onDemandJson Price details json object.
   */
  private void storeEBSPriceComponent(
      InitializationContext context,
      String sku,
      String componentCode,
      Region region,
      JsonNode onDemandJson) {
    // Then create the pricing component object by grabbing the first item (should only have one)
    // and populating the PriceDetails with all the relevant information
    PriceComponent.PriceDetails priceDetails = new PriceComponent.PriceDetails();
    JsonNode product = onDemandJson.get(sku).elements().next();
    JsonNode priceDimensions = product.get("priceDimensions").elements().next();

    // Get the currency & price per unit
    String currency = priceDimensions.get("pricePerUnit").fields().next().getKey();
    String pricePerUnit = priceDimensions.get("pricePerUnit").get(currency).textValue();
    String unit = priceDimensions.get("unit").textValue().toUpperCase();
    if (!(unit.endsWith("-MO") || unit.endsWith("MONTH"))) {
      throw new RuntimeException("Unit is not per month: " + unit);
    }
    priceDetails.currency = PriceComponent.PriceDetails.Currency.valueOf(currency);
    priceDetails.setUnitFromString(unit);
    priceDetails.pricePerUnit = Double.parseDouble(pricePerUnit);
    priceDetails.pricePerMonth = priceDetails.pricePerUnit;
    priceDetails.pricePerDay = priceDetails.pricePerMonth / 30.0;
    priceDetails.pricePerHour = priceDetails.pricePerDay / 24.0;

    // Get everything else
    priceDetails.description = priceDimensions.get("description").textValue();
    priceDetails.effectiveDate = product.get("effectiveDate").textValue();

    // Save to db
    PriceComponent.upsert(
        context.getProvider().getUuid(), region.getCode(), componentCode, priceDetails);
  }

  /**
   * This will store the PriceComponent corresponding to the InstanceType itself. Each price detail
   * object has the format: "DQ578CGN99KG6ECF" : { "DQ578CGN99KG6ECF.JRTCKXETXF" : { "offerTermCode"
   * : "JRTCKXETXF", "sku" : "DQ578CGN99KG6ECF", "effectiveDate" : "2016-08-01T00:00:00Z",
   * "priceDimensions" : { "DQ578CGN99KG6ECF.JRTCKXETXF.6YS6EN2CT7" : { "rateCode" :
   * "DQ578CGN99KG6ECF.JRTCKXETXF.6YS6EN2CT7", "description" : "$4.931 per On Demand Windows
   * hs1.8xlarge Instance Hour", "beginRange" : "0", "endRange" : "Inf", "unit" : "Hrs",
   * "pricePerUnit" : { "USD" : "4.9310000000" }, "appliesTo" : [ ] } }, "termAttributes" : { } } }
   *
   * @param productDetailsListJson Products sub-document with list of EC2 products along with SKU.
   * @param onDemandJson Price details json object.
   */
  private void storeInstancePriceComponents(
      InitializationContext context,
      JsonNode productDetailsListJson,
      JsonNode onDemandJson,
      Region region) {
    Architecture regionArch = region.getArchitecture();
    // Get SKUs associated with Instances
    LOG.info("Parsing product details list to store pricing info");
    for (JsonNode productDetailsJson : productDetailsListJson) {

      Map<String, String> productAttrs = extractAllAttributes(productDetailsJson);
      boolean include = true;

      // Make sure this is a compute instance.
      include &=
          matches(productAttrs, "productFamily", FilterOp.Equals, PRODUCT_FAMILY_COMPUTE_INSTANCE);
      // The service code should be 'AmazonEC2'.
      include &= matches(productAttrs, "servicecode", FilterOp.Equals, "AmazonEC2");
      // Filter by the OS we support.
      include &= (matches(productAttrs, "operatingSystem", FilterOp.Equals, "Linux"));
      // Pick the supported license models.
      include &=
          (matches(productAttrs, "licenseModel", FilterOp.Equals, "No License required")
              || matches(productAttrs, "licenseModel", FilterOp.Equals, "NA"));
      // Pick the valid disk drive types.
      include &=
          (matches(productAttrs, "storage", FilterOp.Contains, "SSD")
              || matches(productAttrs, "storage", FilterOp.Contains, "EBS"));
      // Make sure it is current generation.
      include &= matches(productAttrs, "currentGeneration", FilterOp.Equals, "Yes");
      // Make sure tenancy is shared.
      include &= matches(productAttrs, "tenancy", FilterOp.Equals, "Shared");
      // Make sure it is the base instance type.
      include &= matches(productAttrs, "preInstalledSw", FilterOp.Equals, "NA");
      // Make sure instance type is supported.
      include &= isInstanceTypeSupported(productAttrs);

      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
        // Make sure architecture matches.
        if (regionArch == Architecture.x86_64) {
          include &= matches(productAttrs, "physicalProcessor", FilterOp.Contains, "Intel");
        } else if (regionArch == Architecture.aarch64) {
          include &= matches(productAttrs, "physicalProcessor", FilterOp.Contains, "Graviton");
        }
      }

      if (include) {
        JsonNode attributesJson = productDetailsJson.get("attributes");
        storeInstancePriceComponent(
            context,
            productDetailsJson.get("sku").textValue(),
            attributesJson.get("instanceType").textValue(),
            attributesJson.get("location").textValue(),
            onDemandJson);
      }
    }
  }

  /**
   * Given info about a single AWS InstanceType in a specific region, store its PriceComponent.
   *
   * @param sku SKU of the InstanceType in its region.
   * @param instanceCode Code for the InstanceType (e.g. m3.medium).
   * @param regionName Name for the region the InstanceType is in (e.g. "US West (Oregon)").
   * @param onDemandJson Price details json object.
   */
  private void storeInstancePriceComponent(
      InitializationContext context,
      String sku,
      String instanceCode,
      String regionName,
      JsonNode onDemandJson) {

    // First check that region exists
    Region region =
        Region.find
            .query()
            .where()
            .eq("provider_uuid", context.getProvider().getUuid())
            .eq("name", regionName)
            .findOne();
    if (region == null) {
      LOG.error("Region " + regionName + " not found. Skipping.");
      return;
    }

    // Then create the pricing component object by grabbing the first item (should only have one)
    // and populating the PriceDetails with all the relevant information
    PriceComponent.PriceDetails priceDetails = new PriceComponent.PriceDetails();
    JsonNode product = onDemandJson.get(sku).elements().next();
    JsonNode priceDimensions = product.get("priceDimensions").elements().next();

    // Get the currency & price per unit
    String currency = priceDimensions.get("pricePerUnit").fields().next().getKey();
    String pricePerUnit = priceDimensions.get("pricePerUnit").get(currency).textValue();
    String unit = priceDimensions.get("unit").textValue().toUpperCase();
    if (!(unit.equals("HRS") || unit.equals("HOURS"))) {
      throw new RuntimeException("Unit is not per hour: " + unit);
    }
    priceDetails.setUnitFromString(unit);
    priceDetails.currency = PriceComponent.PriceDetails.Currency.valueOf(currency);
    priceDetails.pricePerUnit = Double.parseDouble(pricePerUnit);
    priceDetails.pricePerHour = priceDetails.pricePerUnit;
    priceDetails.pricePerDay = priceDetails.pricePerUnit * 24.0;
    priceDetails.pricePerMonth = priceDetails.pricePerDay * 30.0;

    // Get everything else
    priceDetails.description = priceDimensions.get("description").textValue();
    priceDetails.effectiveDate = product.get("effectiveDate").textValue();

    // Save to db
    if (Double.parseDouble(pricePerUnit) != 0.0) {
      PriceComponent.upsert(
          context.getProvider().getUuid(), region.getCode(), instanceCode, priceDetails);
    }
  }

  /**
   * Given a JSON blob containing details on various EC2 products, update the ec2AvailableInstances
   * map, which contains information on the various instances available through EC2. Each entry in
   * the product details map looks like: "DQ578CGN99KG6ECF" : { "sku" : "DQ578CGN99KG6ECF",
   * "productFamily" : "Compute Instance", "attributes" : { "servicecode" : "AmazonEC2", "location"
   * : "US East (N. Virginia)", "locationType" : "AWS Region", "instanceType" : "hs1.8xlarge",
   * "currentGeneration" : "No", "instanceFamily" : "Storage optimized", "vcpu" : "17",
   * "physicalProcessor" : "Intel Xeon E5-2650", "clockSpeed" : "2 GHz", "memory" : "117 GiB",
   * "storage" : "24 x 2000", "networkPerformance" : "10 Gigabit", "processorArchitecture" :
   * "64-bit", "tenancy" : "Shared", "operatingSystem" : "Windows", "licenseModel" : "License
   * Included", "usagetype" : "BoxUsage:hs1.8xlarge", "operation" : "RunInstances:0002",
   * "preInstalledSw" : "NA" } }
   *
   * @param productDetailsListJson A JSON blob as described above.
   * @param region The region EC2 product is in.
   */
  private void parseProductDetailsList(
      InitializationContext context, JsonNode productDetailsListJson, Region region) {
    LOG.info("Parsing product details list");
    Iterator<JsonNode> productDetailsListIter = productDetailsListJson.elements();
    Architecture regionArch = region.getArchitecture();
    while (productDetailsListIter.hasNext()) {
      JsonNode productDetailsJson = productDetailsListIter.next();

      Map<String, String> productAttrs = extractAllAttributes(productDetailsJson);

      boolean include = true;

      // Make sure this is a compute instance.
      include &=
          matches(productAttrs, "productFamily", FilterOp.Equals, PRODUCT_FAMILY_COMPUTE_INSTANCE);
      // The service code should be 'AmazonEC2'.
      include &= matches(productAttrs, "servicecode", FilterOp.Equals, "AmazonEC2");
      // Filter by the OS we support.
      include &= (matches(productAttrs, "operatingSystem", FilterOp.Equals, "Linux"));
      // Pick the supported license models.
      include &=
          (matches(productAttrs, "licenseModel", FilterOp.Equals, "No License required")
              || matches(productAttrs, "licenseModel", FilterOp.Equals, "NA"));
      // Pick the valid disk drive types.
      include &=
          (matches(productAttrs, "storage", FilterOp.Contains, "SSD")
              || matches(productAttrs, "storage", FilterOp.Contains, "EBS"));
      // Make sure it is current generation.
      include &= matches(productAttrs, "currentGeneration", FilterOp.Equals, "Yes");
      // Make sure tenancy is shared.
      include &= matches(productAttrs, "tenancy", FilterOp.Equals, "Shared");
      // Make sure it is the base instance type.
      include &= matches(productAttrs, "preInstalledSw", FilterOp.Equals, "NA");
      // Make sure instance type is supported.
      include &= isInstanceTypeSupported(productAttrs);

      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
        // Make sure architecture matches.
        if (regionArch == Architecture.x86_64) {
          include &= matches(productAttrs, "physicalProcessor", FilterOp.Contains, "Intel");
        } else if (regionArch == Architecture.aarch64) {
          include &= matches(productAttrs, "physicalProcessor", FilterOp.Contains, "Graviton");
        }
      }

      if (!include) {
        if (enableVerboseLogging) {
          LOG.info("Skipping product");
        }
        continue;
      }

      if (enableVerboseLogging) {
        LOG.info(
            "Found matching product with sku={}, instanceType={}",
            productAttrs.get("sku"),
            productAttrs.get("instanceType"));
      }
      context.getAvailableInstances().add(productAttrs);
    }
  }

  /**
   * Build a KVP Map for the attributes that make up a given product in the EC2 products JSON.
   *
   * @param productDetailsJson An entry in the EC2 product details JSON list.
   * @return A KVP Map for the attributes of the provided entry.
   */
  private Map<String, String> extractAllAttributes(JsonNode productDetailsJson) {
    Map<String, String> productAttrs = new HashMap<>();
    productAttrs.put("sku", productDetailsJson.get("sku").textValue());
    productAttrs.put(
        "productFamily",
        productDetailsJson.get("productFamily") != null
            ? productDetailsJson.get("productFamily").textValue()
            : "");

    // Iterate over all the attributes.
    Iterator<String> iter = productDetailsJson.get("attributes").fieldNames();
    while (iter.hasNext()) {
      String key = iter.next();
      productAttrs.put(key, productDetailsJson.get("attributes").get(key).textValue());
    }
    if (enableVerboseLogging) {
      StringBuilder sb = new StringBuilder();
      sb.append("Product: sku=").append(productDetailsJson.get("sku").textValue());
      sb.append(", productFamily=").append(productDetailsJson.get("productFamily").textValue());
      for (String key : productAttrs.keySet()) {
        sb.append(", ").append(key).append("=").append(productAttrs.get(key));
      }
      LOG.info(sb.toString());
    }
    return productAttrs;
  }

  /**
   * Store information about the various instance types to the database. Uses UPSERT semantics if
   * the row for the instance type already exists.
   */
  private void storeInstanceTypeInfoToDB(InitializationContext context) {
    LOG.info("Storing AWS instance type and pricing info in Yugaware DB");
    Provider provider = context.getProvider();
    // First reset all the JSON details of all supported instance entries in the table, as we are
    // about to refresh it.
    InstanceType.resetInstanceTypeDetailsForProvider(provider, config, false);
    String instanceTypeCode;

    for (Map<String, String> productAttrs : context.getAvailableInstances()) {
      // Get the instance type.
      instanceTypeCode = productAttrs.get("instanceType");

      // The number of cores is the number of vcpu's.
      if (productAttrs.get("vcpu") == null) {
        String msg = "Error parsing sku=" + productAttrs.get("sku") + ", num vcpu missing";
        LOG.error(msg);
        throw new RuntimeException(msg);
      }
      Integer numCores = Integer.parseInt(productAttrs.get("vcpu"));

      // Parse the memory size.
      String memSizeStrGB =
          productAttrs.get("memory").replaceAll("(?i) gib", "").replaceAll(",", "");
      Double memSizeGB = Double.parseDouble(memSizeStrGB);

      int volumeCount;
      int volumeSizeGB;
      VolumeType volumeType;
      // Parse the local instance store details. Format of the raw data is one of the following:
      // 1 x 75 NVMe SSD
      // EBS only
      // 125 GB NVMe SSD
      // 1 x 800 SSD
      // 12 x 2000 HDD
      // 2 x 900 GB NVMe SSD
      String[] parts = productAttrs.get("storage").replaceAll(",", "").split(" ");
      if (parts.length < 4) {
        if (!productAttrs.get("storage").equals("EBS only")) {
          String msg =
              "Volume type not specified in product sku="
                  + productAttrs.get("sku")
                  + ", storage={"
                  + productAttrs.get("storage")
                  + "}";
          LOG.error(msg);
          throw new UnsupportedOperationException(msg);
        } else {
          // TODO: hardcode me not?
          volumeCount = 0;
          volumeSizeGB = 250;
          volumeType = VolumeType.EBS;
        }
      } else {
        if (parts[1].equals("x")) {
          volumeCount = Integer.parseInt(parts[0]);
          volumeSizeGB = Integer.parseInt(parts[2]);
          if (parts[3].equals("GB")) {
            volumeType = VolumeType.valueOf(parts[4].toUpperCase());
          } else {
            volumeType = VolumeType.valueOf(parts[3].toUpperCase());
          }

        } else {
          volumeCount = 1;
          volumeSizeGB = Integer.parseInt(parts[0]);
          volumeType = VolumeType.valueOf(parts[2].toUpperCase());
        }
      }

      if (enableVerboseLogging) {
        LOG.info(
            "Instance type entry ({}, {}): {} cores, {} GB RAM, {} x {} GB {}",
            provider.getCode(),
            instanceTypeCode,
            numCores,
            memSizeGB,
            volumeCount,
            volumeSizeGB,
            volumeType);
      }

      // Create the instance type model. If one already exists, overwrite it.
      InstanceType instanceType = InstanceType.get(provider.getUuid(), instanceTypeCode);
      if (instanceType == null) {
        instanceType = new InstanceType();
      }
      InstanceTypeDetails details = instanceType.getInstanceTypeDetails();
      if (details == null) {
        details = new InstanceTypeDetails();
      }
      if (details.volumeDetailsList.isEmpty()) {
        details.setVolumeDetailsList(volumeCount, volumeSizeGB, volumeType);
      }
      if (details.tenancy == null) {
        details.tenancy = PublicCloudConstants.Tenancy.Shared;
      }

      if (runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableVMOSPatching)) {
        // Persist the architecture in instance details.
        if (productAttrs.get("physicalProcessor").contains("Intel")) {
          details.arch = Architecture.x86_64;
        } else if (productAttrs.get("physicalProcessor").contains("Graviton")) {
          details.arch = Architecture.aarch64;
        }
      }
      // Update the object.
      InstanceType.upsert(provider.getUuid(), instanceTypeCode, numCores, memSizeGB, details);
      if (enableVerboseLogging) {
        instanceType = InstanceType.get(provider.getUuid(), instanceTypeCode);
        LOG.debug(
            "Saved {}:{} ({} cores, {}GB) with details {}",
            provider.getUuid(),
            instanceTypeCode,
            instanceType.getNumCores(),
            instanceType.getMemSizeGB(),
            Json.stringify(Json.toJson(details)));
      }
    }
  }

  enum FilterOp {
    Equals,
    Contains,
  }

  private boolean matches(Map<String, String> objAttrs, String name, FilterOp op, String value) {
    switch (op) {
      case Equals:
        return value.equals(objAttrs.get(name));
      case Contains:
        return objAttrs.containsKey(name) && objAttrs.get(name).contains(value);
      default:
        return false;
    }
  }

  private boolean isInstanceTypeSupported(Map<String, String> productAttributes) {
    return InstanceType.getAWSInstancePrefixesSupported(config).stream()
        .anyMatch(productAttributes.getOrDefault("instanceType", "")::startsWith);
  }
}
