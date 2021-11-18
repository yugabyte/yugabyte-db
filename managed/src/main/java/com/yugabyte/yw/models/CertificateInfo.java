// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.CertificateParams;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
public class CertificateInfo extends Model {

  public enum Type {
    @EnumValue("SelfSigned")
    SelfSigned,

    @EnumValue("CustomCertHostPath")
    CustomCertHostPath
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
    if (this.customCertInfo != null) {
      return Json.fromJson(this.customCertInfo, CertificateParams.CustomCertInfo.class);
    }
    return null;
  }

  public void setCustomCertInfo(CertificateParams.CustomCertInfo certInfo) {
    this.customCertInfo = Json.toJson(certInfo);
    this.save();
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

  public static CertificateInfo createCopy(
      CertificateInfo certificateInfo, String label, String certFilePath)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo copy = new CertificateInfo();
    copy.uuid = UUID.randomUUID();
    copy.customerUUID = certificateInfo.customerUUID;
    copy.label = label;
    copy.startDate = certificateInfo.startDate;
    copy.expiryDate = certificateInfo.expiryDate;
    copy.privateKey = certificateInfo.privateKey;
    copy.certificate = certFilePath;
    copy.certType = certificateInfo.certType;
    copy.checksum = Util.getFileChecksum(certFilePath);
    copy.customCertInfo = certificateInfo.customCertInfo;
    copy.save();
    return copy;
  }

  public static boolean isTemporary(CertificateInfo certificateInfo) {
    return certificateInfo.certificate.endsWith("ca.multi.root.crt");
  }

  private static final Finder<UUID, CertificateInfo> find =
      new Finder<UUID, CertificateInfo>(CertificateInfo.class) {};

  public static CertificateInfo get(UUID certUUID) {
    return find.byId(certUUID);
  }

  public static CertificateInfo get(String label) {
    return find.query().where().eq("label", label).findOne();
  }

  public static List<CertificateInfo> getWhereLabelStartsWith(String label, Type certType) {
    List<CertificateInfo> certificateInfoList =
        find.query().where().eq("cert_type", certType).like("label", label + "%").findList();
    return certificateInfoList
        .stream()
        .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
        .collect(Collectors.toList());
  }

  public static List<CertificateInfo> getAllNoChecksum() {
    List<CertificateInfo> certificateInfoList = find.query().where().isNull("checksum").findList();
    return certificateInfoList
        .stream()
        .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
        .collect(Collectors.toList());
  }

  public static List<CertificateInfo> getAll(UUID customerUUID) {
    List<CertificateInfo> certificateInfoList =
        find.query().where().eq("customer_uuid", customerUUID).findList();
    return certificateInfoList
        .stream()
        .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
        .collect(Collectors.toList());
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
}
