// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.avaje.ebean.Ebean;
import com.avaje.ebean.Model;
import com.avaje.ebean.SqlUpdate;
import com.avaje.ebean.annotation.EnumValue;

import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class InstanceType extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(InstanceType.class);
  public enum VolumeType {
    @EnumValue("EBS")
    EBS,

    @EnumValue("SSD")
    SSD,

    @EnumValue("HDD")
    HDD
  }

  @EmbeddedId
  @Constraints.Required
  public InstanceTypeKey idKey;

  public String getProviderCode() { return this.idKey.providerCode; }

  public String getInstanceTypeCode() { return this.idKey.instanceTypeCode; }

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "boolean default true")
  private Boolean active = true;
  public Boolean isActive() { return active; }
  public void setActive(Boolean active) { this.active = active; }

  @Constraints.Required
  @Column(nullable = false)
  public Integer numCores;

  @Constraints.Required
  @Column(nullable = false)
  public Double memSizeGB;

  // The number of instance-local drives. Not valid for EBS.
  @Constraints.Required
  @Column(nullable = false)
  public Integer volumeCount;

  // The size of each instance-local drive.  Not valid for EBS.
  @Constraints.Required
  @Column(nullable = false)
  public Integer volumeSizeGB;

  // The type of the volume.
  @Constraints.Required
  @Column(nullable = false)
  public VolumeType volumeType;

  @Column(columnDefinition = "LONGTEXT")
  private String instanceTypeDetailsJson;
  public InstanceTypeDetails instanceTypeDetails;

  private static final Find<InstanceTypeKey, InstanceType> find =
      new Find<InstanceTypeKey, InstanceType>() {};

  public static InstanceType get(String providerCode, String instanceTypeCode) {
    InstanceType instanceType = find.byId(InstanceTypeKey.create(instanceTypeCode, providerCode));
    if (instanceType == null) {
      return instanceType;
    }
    // Since 'instanceTypeDetailsJson' can be null (populated externally), we need to populate these
    // fields explicitly.
    if (instanceType.instanceTypeDetailsJson == null) {
      instanceType.instanceTypeDetails = new InstanceTypeDetails();
      instanceType.instanceTypeDetailsJson =
          Json.stringify(Json.toJson(instanceType.instanceTypeDetails));
    } else {
    instanceType.instanceTypeDetails =
        Json.fromJson(Json.parse(instanceType.instanceTypeDetailsJson), InstanceTypeDetails.class);
    }
    return instanceType;
  }

  public static InstanceType upsert(String providerCode,
                                    String instanceTypeCode,
                                    Integer numCores,
                                    Double memSize,
                                    Integer volumeCount,
                                    Integer volumeSize,
                                    VolumeType volumeType,
                                    InstanceTypeDetails instanceTypeDetails) {
    InstanceType instanceType = InstanceType.get(providerCode, instanceTypeCode);
    if (instanceType == null) {
      instanceType = new InstanceType();
      instanceType.idKey = InstanceTypeKey.create(instanceTypeCode, providerCode);
    }
    instanceType.volumeCount = volumeCount;
    instanceType.volumeSizeGB = volumeSize;
    instanceType.volumeType = volumeType;
    instanceType.memSizeGB = memSize;
    instanceType.numCores = numCores;
    instanceType.instanceTypeDetails = instanceTypeDetails;
    instanceType.instanceTypeDetailsJson = Json.stringify(Json.toJson(instanceTypeDetails));
    LOG.debug("Saving {} ({} cores, {}GB, {}x{}GB {}) with details {}",
              instanceType.idKey.toString(), instanceType.numCores, instanceType.memSizeGB,
              instanceType.volumeCount, instanceType.volumeSizeGB,
              instanceType.volumeType.toString(), instanceType.instanceTypeDetailsJson);
    // Update the in-memory fields.
    instanceType.save();
    // Update the JSON field - this does not seem to be updated by the save above.
    String updateQuery = "UPDATE instance_type " +
        "SET instance_type_details_json = :instanceTypeDetails " +
        "WHERE provider_code = :providerCode AND instance_type_code = :instanceTypeCode";
    SqlUpdate update = Ebean.createSqlUpdate(updateQuery);
    update.setParameter("instanceTypeDetails", instanceType.instanceTypeDetailsJson);
    update.setParameter("providerCode", providerCode);
    update.setParameter("instanceTypeCode", instanceTypeCode);
    int modifiedCount = Ebean.execute(update);
    // Check if the save was not successful.
    if (modifiedCount == 0) {
      // Throw an exception as the save was not successful.
      LOG.error("Failed to update SQL row");
    } else if (modifiedCount > 1) {
      // Exactly one row should have been modified.
      LOG.error("Running query [" + updateQuery + "] updated " + modifiedCount + " rows");
    }
    return instanceType;
  }

  /**
   * Query Helper to find supported instance types for a given cloud provider.
   */
  public static List<InstanceType> findByProvider(Provider provider) {
    List<InstanceType> entries =
        InstanceType.find.where().eq("provider_code", provider.code).findList();
    List<InstanceType> instanceTypes = new ArrayList<InstanceType>();
    for (InstanceType entry : entries) {
      InstanceType instanceType =
          InstanceType.get(entry.getProviderCode(), entry.getInstanceTypeCode());
      instanceTypes.add(instanceType);
    }
    return instanceTypes;
  }

  public static class InstanceTypeDetails {
    public Map<String, InstanceTypeRegionDetails> regionCode;

    public InstanceTypeDetails() {
      regionCode = new HashMap<String, InstanceTypeRegionDetails>();
    }
  }

  public static class InstanceTypeRegionDetails {
    // This should be "AmazonEC2". Present for debugging/verification.
    public String servicecode;

    // For compute, this is "Compute Instance". For elastic IP, this is "IP Address". Present for
    // debugging/verification.
    public String productFamily;

    // The valid values here are "Host", "Dedicated" and "Shared".
    public String tenancy;

    // Known values are "Linux", "RHEL", "SUSE", "Windows", "NA".
    public String operatingSystem;

    // The pricing details.
    public List<PriceDetails> priceDetailsList;
  }

  public static class PriceDetails {
    // The unit on which the 'pricePerUnit' is based.
    public enum Unit {
      Hours
    }

    // The price currency. Note that the case here matters as it matches AWS output.
    public enum Currency {
      USD
    }

    // The unit.
    public Unit unit;

    // Price per unit.
    public double pricePerUnit;

    // Currency.
    public Currency currency;

    // This denotes the start and end range for the pricing, as sometimes pricing is tiered, for
    // instance by the number of hours used, etc.
    public String beginRange;
    public String endRange;

    // Keeping these around for now as they seem useful.
    public String effectiveDate;
    public String description;

  }
}
