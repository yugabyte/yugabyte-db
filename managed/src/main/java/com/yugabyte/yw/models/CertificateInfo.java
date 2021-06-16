// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;

import io.ebean.*;
import io.ebean.annotation.*;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.YWServiceException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.data.validation.Constraints;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Enumerated;
import javax.persistence.EnumType;
import javax.persistence.Id;

import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.Set;
import java.util.stream.Collectors;

import static play.mvc.Http.Status.*;

@Entity
public class CertificateInfo extends Model {

  public enum Type {
    @EnumValue("SelfSigned")
    SelfSigned,

    @EnumValue("CustomCertHostPath")
    CustomCertHostPath,

    @EnumValue("CustomServerCert")
    CustomServerCert
  }

  public static class CustomServerCertInfo {
    public String serverCert;
    public String serverKey;

    public CustomServerCertInfo() {
      this.serverCert = null;
      this.serverKey = null;
    }

    public CustomServerCertInfo(String serverCert, String serverKey) {
      this.serverCert = serverCert;
      this.serverKey = serverKey;
    }
  }

  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  public UUID uuid;

  @Constraints.Required
  @Column(nullable = false)
  public UUID customerUUID;

  @Column(unique = true)
  public String label;

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date startDate;

  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date expiryDate;

  @Column(nullable = true)
  public String privateKey;

  @Constraints.Required
  @Column(nullable = false)
  public String certificate;

  @Constraints.Required
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  public CertificateInfo.Type certType;

  @Column(nullable = true)
  public String checksum;

  public void setChecksum() throws IOException, NoSuchAlgorithmException {
    if (this.certificate != null) {
      this.checksum = Util.getFileChecksum(this.certificate);
      this.save();
    }
  }

  @Column(columnDefinition = "TEXT", nullable = true)
  @DbJson
  public JsonNode customCertInfo;

  public CertificateParams.CustomCertInfo getCustomCertInfo() {
    if (this.certType != CertificateInfo.Type.CustomCertHostPath) {
      return null;
    }
    if (this.customCertInfo != null) {
      return Json.fromJson(this.customCertInfo, CertificateParams.CustomCertInfo.class);
    }
    return null;
  }

  public void setCustomCertInfo(
      CertificateParams.CustomCertInfo certInfo, UUID certUUID, UUID cudtomerUUID) {
    this.checkEditable(certUUID, customerUUID);
    this.customCertInfo = Json.toJson(certInfo);
    this.save();
  }

  public CustomServerCertInfo getCustomServerCertInfo() {
    if (this.certType != CertificateInfo.Type.CustomServerCert) {
      return null;
    }
    if (this.customCertInfo != null) {
      return Json.fromJson(this.customCertInfo, CustomServerCertInfo.class);
    }
    return null;
  }

  public static final Logger LOG = LoggerFactory.getLogger(CertificateInfo.class);

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String privateKey,
      String certificate,
      CertificateInfo.Type certType)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.uuid = uuid;
    cert.customerUUID = customerUUID;
    cert.label = label;
    cert.startDate = startDate;
    cert.expiryDate = expiryDate;
    cert.privateKey = privateKey;
    cert.certificate = certificate;
    cert.certType = certType;
    cert.checksum = Util.getFileChecksum(certificate);
    cert.save();
    return cert;
  }

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String certificate,
      CertificateParams.CustomCertInfo customCertInfo)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.uuid = uuid;
    cert.customerUUID = customerUUID;
    cert.label = label;
    cert.startDate = startDate;
    cert.expiryDate = expiryDate;
    cert.certificate = certificate;
    cert.certType = Type.CustomCertHostPath;
    cert.customCertInfo = Json.toJson(customCertInfo);
    cert.checksum = Util.getFileChecksum(certificate);
    cert.save();
    return cert;
  }

  public static CertificateInfo create(
      UUID uuid,
      UUID customerUUID,
      String label,
      Date startDate,
      Date expiryDate,
      String certificate,
      CustomServerCertInfo customServerCertInfo)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.uuid = uuid;
    cert.customerUUID = customerUUID;
    cert.label = label;
    cert.startDate = startDate;
    cert.expiryDate = expiryDate;
    cert.certificate = certificate;
    cert.certType = Type.CustomServerCert;
    cert.customCertInfo = Json.toJson(customServerCertInfo);
    cert.checksum = Util.getFileChecksum(certificate);
    cert.save();
    return cert;
  }

  private static final Finder<UUID, CertificateInfo> find =
      new Finder<UUID, CertificateInfo>(CertificateInfo.class) {};

  public static CertificateInfo get(UUID certUUID) {
    return find.byId(certUUID);
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    if (!certificateInfo.customerUUID.equals(customerUUID)) {
      throw new YWServiceException(BAD_REQUEST, "Certificate doesn't belong to customer");
    }
    return certificateInfo;
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    return certificateInfo;
  }

  public static CertificateInfo get(String label) {
    return find.query().where().eq("label", label).findOne();
  }

  public static CertificateInfo getOrBadRequest(String label) {
    CertificateInfo certificateInfo = get(label);
    if (certificateInfo == null) {
      throw new YWServiceException(BAD_REQUEST, "No Certificate with Label: " + label);
    }
    return certificateInfo;
  }

  public static List<CertificateInfo> getAllNoChecksum() {
    return find.query().where().isNull("checksum").findList();
  }

  public static List<CertificateInfo> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static boolean isCertificateValid(UUID certUUID) {
    if (certUUID == null) {
      return true;
    }
    CertificateInfo certificate = CertificateInfo.get(certUUID);
    if (certificate == null) {
      return false;
    }
    if (certificate.certType == CertificateInfo.Type.CustomCertHostPath
        && certificate.customCertInfo == null) {
      return false;
    }
    return true;
  }

  // Returns if there is an in use reference to the object.
  public boolean getInUse() {
    return Universe.existsCertificate(this.uuid, this.customerUUID);
  }

  public ArrayNode getUniverseDetails() {
    Set<Universe> universes = Universe.universeDetailsIfCertsExists(this.uuid, this.customerUUID);
    return Util.getUniverseDetails(universes);
  }

  public static void delete(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificate = CertificateInfo.getOrBadRequest(certUUID, customerUUID);
    if (!certificate.getInUse()) {
      if (certificate.delete()) {
        LOG.info("Successfully deleted the certificate:" + certUUID);
      } else {
        throw new YWServiceException(INTERNAL_SERVER_ERROR, "Unable to delete the Certificate");
      }
    } else {
      throw new YWServiceException(BAD_REQUEST, "The certificate is in use.");
    }
  }

  private void checkEditable(UUID certUUID, UUID customerUUID) {
    CertificateInfo certInfo = getOrBadRequest(certUUID, customerUUID);
    if (certInfo.certType == CertificateInfo.Type.SelfSigned) {
      throw new YWServiceException(BAD_REQUEST, "Cannot edit self-signed cert.");
    }
    if (certInfo.customCertInfo != null) {
      throw new YWServiceException(
          BAD_REQUEST, "Cannot edit pre-customized cert. Create a new one.");
    }
  }
}
