// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.EnumValue;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

import javax.persistence.*;
import java.util.List;
import java.util.UUID;

@Entity
public class InstanceType extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(InstanceType.class);
  public enum VolumeType {
    @EnumValue("EBS")
    EBS,

    @EnumValue("SSD")
    SSD
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
  public Double memSize;

  @Constraints.Required
  @Column(nullable = false)
  public Integer volumeSize;

  @Constraints.Required
  @Column(nullable = false)
  public VolumeType volumeType;

  /**
   * Query Helper for InstanceType with uuid
   */
  public static final Find<UUID, InstanceType> find = new Find<UUID, InstanceType>(){};

  public static List<InstanceType> findByProvider(Provider provider) {
    return InstanceType.find.where().eq("provider_code", provider.code).findList();
  }

  public static InstanceType create(String providerCode,
                                    String code,
                                    Integer numOfCores,
                                    Double memSize,
                                    Integer volumeSize,
                                    VolumeType volumeType) {

    InstanceType instanceType = new InstanceType();
    instanceType.idKey = InstanceTypeKey.create(code, providerCode);
    instanceType.volumeSize = volumeSize;
    instanceType.volumeType = volumeType;
    instanceType.memSize = memSize;
    instanceType.numCores = numOfCores;
    instanceType.save();
    return instanceType;
  }
}
