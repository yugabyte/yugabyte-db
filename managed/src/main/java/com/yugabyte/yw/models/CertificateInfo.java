// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.Transient;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.models.helpers.CommonUtils;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;
import play.libs.Json;

@ApiModel(description = "SSL certificate used by the universe")
@Entity
public class CertificateInfo extends Model {

  /**
   * This is the custom certificatePath information certificates received in param are converted to
   * certs and dumped in file This contains information of file path for respective certs
   */
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

  @ApiModelProperty(value = "Certificate UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Id
  @Column(nullable = false, unique = true)
  public UUID uuid;

  @ApiModelProperty(
      value = "Customer UUID of the backup which it belongs to",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  public UUID customerUUID;

  @ApiModelProperty(
      value = "Certificate label",
      example = "yb-admin-example",
      accessMode = READ_WRITE)
  @Column(unique = true)
  public String label;

  @ApiModelProperty(value = "The certificate's creation date", accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date startDate;

  @ApiModelProperty(value = "The certificate's expiry date", accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd HH:mm:ss")
  public Date expiryDate;

  @ApiModelProperty(
      value = "Private key path",
      example = "/opt/yugaware/.../example.key.pem",
      accessMode = READ_WRITE)
  @Column(nullable = true)
  public String privateKey;

  @ApiModelProperty(
      value = "Certificate path",
      example = "/opt/yugaware/certs/.../ca.root.cert",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  public String certificate;

  @ApiModelProperty(
      value = "Type of the certificate",
      example = "SelfSigned",
      accessMode = READ_WRITE)
  @Constraints.Required
  @Column(nullable = false)
  @Enumerated(EnumType.STRING)
  public CertConfigType certType;

  @ApiModelProperty(value = "The certificate file's checksum", accessMode = READ_ONLY)
  @Column(nullable = true)
  public String checksum;

  public void setChecksum() throws IOException, NoSuchAlgorithmException {
    if (this.certificate != null) {
      this.checksum = FileUtils.getFileChecksum(this.certificate);
      this.save();
    }
  }

  @ApiModelProperty(value = "Details about the certificate", accessMode = READ_WRITE)
  @Column(columnDefinition = "TEXT", nullable = true)
  @DbJson
  // @JsonIgnore
  public JsonNode customCertInfo;

  public CertificateParams.CustomCertInfo getCustomCertPathParams() {
    if (this.certType != CertConfigType.CustomCertHostPath) {
      return null;
    }
    if (this.customCertInfo != null) {
      return Json.fromJson(this.customCertInfo, CertificateParams.CustomCertInfo.class);
    }
    return null;
  }

  public void setCustomCertPathParams(
      CertificateParams.CustomCertInfo certInfo, UUID certUUID, UUID cudtomerUUID) {
    this.checkEditable(certUUID, customerUUID);
    this.customCertInfo = Json.toJson(certInfo);
    this.save();
  }

  public CustomServerCertInfo getCustomServerCertInfo() {
    if (this.certType != CertConfigType.CustomServerCert) {
      return null;
    }
    if (this.customCertInfo != null) {
      return Json.fromJson(this.customCertInfo, CustomServerCertInfo.class);
    }
    return null;
  }

  /**
   * To be called to return response to API calls, for java.lang.reflect
   *
   * @return
   */
  public HashicorpVaultConfigParams getCustomHCPKICertInfo() {
    if (this.certType != CertConfigType.HashicorpVault) {
      return null;
    }
    if (this.customCertInfo == null) {
      return null;
    }

    HashicorpVaultConfigParams params = getCustomHCPKICertInfoInternal();
    params.vaultToken = CommonUtils.getMaskedValue("HC_VAULT_TOKEN", params.vaultToken);
    return params;
  }

  @JsonIgnore
  public HashicorpVaultConfigParams getCustomHCPKICertInfoInternal() {
    if (this.certType != CertConfigType.HashicorpVault) {
      return null;
    }
    if (this.customCertInfo == null) {
      return null;
    }

    HashicorpVaultConfigParams params = new HashicorpVaultConfigParams(this.customCertInfo);
    String token =
        EncryptionInTransitUtil.unmaskCertConfigData(this.customerUUID, params.vaultToken);
    if (token != null) params.vaultToken = token;

    return params;
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
      CertConfigType certType)
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
    cert.checksum = FileUtils.getFileChecksum(certificate);
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
    cert.certType = CertConfigType.CustomCertHostPath;
    cert.customCertInfo = Json.toJson(customCertInfo);
    cert.checksum = FileUtils.getFileChecksum(certificate);
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
    cert.certType = CertConfigType.CustomServerCert;
    cert.customCertInfo = Json.toJson(customServerCertInfo);
    cert.checksum = FileUtils.getFileChecksum(certificate);
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
      HashicorpVaultConfigParams params)
      throws IOException, NoSuchAlgorithmException {
    CertificateInfo cert = new CertificateInfo();
    cert.uuid = uuid;
    cert.customerUUID = customerUUID;
    cert.label = label;
    cert.startDate = startDate;
    cert.expiryDate = expiryDate;
    cert.certificate = certificate;
    cert.certType = CertConfigType.HashicorpVault;
    JsonNode node = params.toJsonNode();
    if (node != null) cert.customCertInfo = node;
    cert.checksum = FileUtils.getFileChecksum(certificate);
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
    copy.checksum = FileUtils.getFileChecksum(certFilePath);
    copy.customCertInfo = certificateInfo.customCertInfo;
    copy.save();
    return copy;
  }

  public CertificateInfo update(
      Date sDate, Date eDate, String certPath, HashicorpVaultConfigParams params)
      throws IOException, NoSuchAlgorithmException {

    LOG.info("Updating uuid: {} with Path:{}", uuid.toString(), certPath);

    if (sDate != null) startDate = sDate;
    if (eDate != null) expiryDate = eDate;

    certificate = certPath;

    JsonNode node = params.toJsonNode();
    if (node != null) customCertInfo = node;

    checksum = FileUtils.getFileChecksum(certificate);
    save();
    return this;
  }

  public static boolean isTemporary(CertificateInfo certificateInfo) {
    return certificateInfo.certificate.endsWith("ca.multi.root.crt");
  }

  private static final Finder<UUID, CertificateInfo> find =
      new Finder<UUID, CertificateInfo>(CertificateInfo.class) {};

  public static CertificateInfo get(UUID certUUID) {
    return find.byId(certUUID);
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    if (!certificateInfo.customerUUID.equals(customerUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Certificate doesn't belong to customer");
    }
    return certificateInfo;
  }

  public static CertificateInfo getOrBadRequest(UUID certUUID) {
    CertificateInfo certificateInfo = get(certUUID);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Cert ID: " + certUUID);
    }
    return certificateInfo;
  }

  public static CertificateInfo get(String label) {
    return find.query().where().eq("label", label).findOne();
  }

  public static CertificateInfo getOrBadRequest(String label) {
    CertificateInfo certificateInfo = get(label);
    if (certificateInfo == null) {
      throw new PlatformServiceException(BAD_REQUEST, "No Certificate with Label: " + label);
    }
    return certificateInfo;
  }

  public static List<CertificateInfo> getWhereLabelStartsWith(
      String label, CertConfigType certType) {
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

    certificateInfoList =
        certificateInfoList
            .stream()
            .filter(certificateInfo -> !CertificateInfo.isTemporary(certificateInfo))
            .collect(Collectors.toList());

    populateUniverseData(customerUUID, certificateInfoList);
    return certificateInfoList;
  }

  public static boolean isCertificateValid(UUID certUUID) {
    if (certUUID == null) {
      return true;
    }
    CertificateInfo certificate = CertificateInfo.get(certUUID);
    if (certificate == null) {
      return false;
    }
    if (certificate.certType == CertConfigType.CustomCertHostPath
        && certificate.customCertInfo == null) {
      return false;
    }
    return true;
  }

  @VisibleForTesting @Transient Boolean inUse = null;

  @ApiModelProperty(
      value =
          "Indicates whether the certificate is in use. This value is `true` if the universe contains a reference to the certificate.",
      accessMode = READ_ONLY)
  // Returns if there is an in use reference to the object.
  public boolean getInUse() {
    if (inUse == null) {
      return Universe.existsCertificate(this.uuid, this.customerUUID);
    } else {
      return inUse;
    }
  }

  public void setInUse(boolean inUse) {
    this.inUse = inUse;
  }

  @VisibleForTesting @Transient List<UniverseDetailSubset> universeDetailSubsets = null;

  @ApiModelProperty(
      value = "Associated universe details for the certificate",
      accessMode = READ_ONLY)
  public List<UniverseDetailSubset> getUniverseDetails() {
    if (universeDetailSubsets == null) {
      Set<Universe> universes = Universe.universeDetailsIfCertsExists(this.uuid, this.customerUUID);
      return Util.getUniverseDetails(universes);
    } else {
      return universeDetailSubsets;
    }
  }

  public void setUniverseDetails(List<UniverseDetailSubset> universeDetailSubsets) {
    this.universeDetailSubsets = universeDetailSubsets;
  }

  public static void populateUniverseData(
      UUID customerUUID, List<CertificateInfo> certificateInfoList) {
    Set<Universe> universes = Customer.get(customerUUID).getUniverses();
    Set<UUID> certificateInfoSet =
        certificateInfoList.stream().map(e -> e.uuid).collect(Collectors.toSet());

    Map<UUID, Set<Universe>> certificateUniverseMap = new HashMap<>();
    universes.forEach(
        universe -> {
          UUID rootCA = universe.getUniverseDetails().rootCA;
          UUID clientRootCA = universe.getUniverseDetails().clientRootCA;
          if (rootCA != null) {
            if (certificateInfoSet.contains(rootCA)) {
              certificateUniverseMap.putIfAbsent(rootCA, new HashSet<>());
              certificateUniverseMap.get(rootCA).add(universe);
            } else {
              LOG.error("Universe: {} has unknown rootCA: {}", universe.universeUUID, rootCA);
            }
          }
          if (clientRootCA != null && !clientRootCA.equals(rootCA)) {
            if (certificateInfoSet.contains(clientRootCA)) {
              certificateUniverseMap.putIfAbsent(clientRootCA, new HashSet<>());
              certificateUniverseMap.get(clientRootCA).add(universe);
            } else {
              LOG.error("Universe: {} has unknown clientRootCA: {}", universe.universeUUID, rootCA);
            }
          }
        });

    certificateInfoList.forEach(
        certificateInfo -> {
          if (certificateUniverseMap.containsKey(certificateInfo.uuid)) {
            certificateInfo.setInUse(true);
            certificateInfo.setUniverseDetails(
                Util.getUniverseDetails(certificateUniverseMap.get(certificateInfo.uuid)));
          } else {
            certificateInfo.setInUse(false);
            certificateInfo.setUniverseDetails(new ArrayList<>());
          }
        });
  }

  public static void delete(UUID certUUID, UUID customerUUID) {
    CertificateInfo certificate = CertificateInfo.getOrBadRequest(certUUID, customerUUID);
    if (!certificate.getInUse()) {
      if (certificate.delete()) {
        LOG.info("Successfully deleted the certificate: " + certUUID);
      } else {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Unable to delete the Certificate");
      }
    } else {
      throw new PlatformServiceException(BAD_REQUEST, "The certificate is in use.");
    }
  }

  public void checkEditable(UUID certUUID, UUID customerUUID) {
    CertificateInfo certInfo = getOrBadRequest(certUUID, customerUUID);
    if (certInfo.certType == CertConfigType.SelfSigned) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot edit self-signed cert.");
    }
    if (!(certInfo.customCertInfo == null || certInfo.customCertInfo.isNull())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit pre-customized cert. Create a new one.");
    }
  }
}
