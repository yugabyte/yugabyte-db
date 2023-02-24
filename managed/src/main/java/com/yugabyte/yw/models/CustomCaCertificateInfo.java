// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@ApiModel(description = "Custom CA certificate")
@Entity
@Getter
@Setter
@Slf4j
public class CustomCaCertificateInfo extends Model {

  private String name;

  @Id private UUID id;

  private UUID customerId;

  @ApiModelProperty(
      value = "Path to CA Certificate",
      example = "/opt/yugaware/certs/trust-store/<ID>/ca.root.cert")
  private String contents;

  @ApiModelProperty(value = "Start date of certificate validity.", example = "2023-10-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date startDate;

  @ApiModelProperty(value = "End date of certificate validity.", example = "2024-07-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date expiryDate;

  private boolean active;

  @ApiModelProperty(value = "Date when certificate was added.", example = "2023-11-10T15:09:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  private Date createdTime;

  private static final Finder<UUID, CustomCaCertificateInfo> finder =
      new Finder<>(CustomCaCertificateInfo.class) {};

  public static List<CustomCaCertificateInfo> getAll(boolean noContents) {
    // Fetch certs ordered by created sequence; index ix_custom_cacert_created will be used
    List<CustomCaCertificateInfo> certs = finder.query().where().order("createdTime").findList();
    if (certs != null && noContents) {
      certs.forEach(cert -> cert.setContents(null));
    }
    return certs;
  }

  public static CustomCaCertificateInfo get(UUID customerId, UUID certId, boolean noContents) {
    CustomCaCertificateInfo cert = finder.byId(certId);
    if (cert != null) {
      if (noContents) cert.setContents(null);
    }
    return cert;
  }

  public static boolean delete(UUID customerId, UUID certId) {
    CustomCaCertificateInfo cert = get(customerId, certId, false);
    if (cert == null) {
      log.error(String.format("Certificate %s does not exist", certId));
      return false;
    }
    return cert.delete();
  }

  public static CustomCaCertificateInfo create(
      UUID customerId,
      UUID certId,
      String name,
      String contents,
      Date startDate,
      Date expiryDate,
      boolean active) {
    CustomCaCertificateInfo cert = new CustomCaCertificateInfo();
    cert.setId(certId);
    cert.setCustomerId(customerId);
    cert.setName(name);
    cert.setContents(contents);
    cert.setStartDate(startDate);
    cert.setExpiryDate(expiryDate);
    cert.setActive(active);
    cert.setCreatedTime(new Date());
    cert.save();
    return cert;
  }

  public static CustomCaCertificateInfo getOrGrunt(
      UUID customerId, UUID certId, boolean noContents) {
    CustomCaCertificateInfo cert = get(customerId, certId, noContents);
    if (cert == null) {
      String msg = String.format("No such certificate %s for customer %s", certId, customerId);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }
    return cert;
  }

  public void deactivate() {
    setActive(false);
    save();
  }

  public void activate() {
    setActive(true);
    save();
  }

  public static CustomCaCertificateInfo getOrGrunt(UUID customerId, UUID certId) {
    boolean noContents = false; // return contents also.
    CustomCaCertificateInfo cert = getOrGrunt(customerId, certId, noContents);
    // Add actual contents to it.
    String filePath = cert.getContents();
    cert.setContents(getCACertData(filePath));
    return cert;
  }

  public static CustomCaCertificateInfo getByName(String name) {
    log.debug("Getting certificate by name {}", name);
    List<CustomCaCertificateInfo> certs =
        finder.query().where().eq("name", name).eq("active", true).findList();
    return certs != null && !certs.isEmpty() ? certs.get(0) : null;
  }

  private static String getCACertData(String cert_path) {
    log.debug("Getting certificate string from {}", cert_path);
    byte[] byteData = FileData.getDecodedData(cert_path);
    String strCert = new String(byteData, StandardCharsets.UTF_8);
    log.debug("CA certificate data is {}", strCert);
    return strCert;
  }
}
