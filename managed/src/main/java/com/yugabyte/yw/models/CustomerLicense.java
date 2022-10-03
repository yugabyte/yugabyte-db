// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;

import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.Getter;

import java.io.File;
import java.util.Date;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import play.data.validation.Constraints;

@Entity
@ApiModel(
    description =
        "Customer Licenses. This helps customer to "
            + "upload licenses for the thirdparrty softwares if required.")
public class CustomerLicense extends Model {

  @ApiModelProperty(value = "License UUID", accessMode = AccessMode.READ_ONLY)
  @Id
  public UUID licenseUUID;

  @ApiModelProperty(
      value = "Customer UUID that owns this license",
      accessMode = AccessMode.READ_WRITE)
  @Column(nullable = false)
  public UUID customerUUID;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "License File Path", required = true)
  public String license;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Type of the license", required = true)
  public String licenseType;

  @Column(nullable = false)
  @ApiModelProperty(
      value = "Creation date of license",
      required = false,
      accessMode = AccessMode.READ_ONLY)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  @Getter
  public Date creationDate;

  public void setCreationDate() {
    this.creationDate = new Date();
  }

  public static CustomerLicense create(UUID customerUUID, String license, String licenseType) {
    CustomerLicense cLicense = new CustomerLicense();
    cLicense.licenseUUID = UUID.randomUUID();
    cLicense.customerUUID = customerUUID;
    cLicense.licenseType = licenseType;
    cLicense.license = license;
    cLicense.setCreationDate();
    cLicense.save();
    return cLicense;
  }

  public static final Finder<UUID, CustomerLicense> find =
      new Finder<UUID, CustomerLicense>(CustomerLicense.class) {};

  public static CustomerLicense get(UUID licenseUUID) {
    return find.byId(licenseUUID);
  }

  public static CustomerLicense getOrBadRequest(UUID licenseUUID) {
    CustomerLicense cLicense = get(licenseUUID);
    if (cLicense == null) {
      throw new PlatformServiceException(BAD_REQUEST, "License not found: " + licenseUUID);
    }
    return cLicense;
  }
}
