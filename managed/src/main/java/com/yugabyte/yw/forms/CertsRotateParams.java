// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CertificateHelper;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import play.mvc.Http.Status;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = CertsRotateParams.Converter.class)
public class CertsRotateParams extends UpgradeTaskParams {

  // The certificate that needs to be used.
  public UUID certUUID = null;
  // If the root certificate needs to be rotated.
  public boolean rotateRoot = false;

  public CertsRotateParams() {}

  @JsonCreator
  public CertsRotateParams(@JsonProperty(value = "certUUID", required = true) UUID certUUID) {
    this.certUUID = certUUID;
  }

  @Override
  public void verifyParams(Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    CertificateInfo cert = CertificateInfo.get(certUUID);
    UUID rootCA = universe.getUniverseDetails().rootCA;
    CertificateInfo rootCert = CertificateInfo.get(rootCA);

    super.verifyParams(universe);

    if (upgradeOption == UpgradeOption.NON_RESTART_UPGRADE) {
      throw new YWServiceException(Status.BAD_REQUEST, "Cert upgrade cannot be non restart.");
    }

    if (!userIntent.providerType.equals(CloudType.onprem)) {
      throw new YWServiceException(Status.BAD_REQUEST, "Certs can only be rotated for onprem.");
    }

    if (certUUID == null) {
      throw new YWServiceException(Status.BAD_REQUEST, "CertUUID cannot be null");
    }

    if (cert == null) {
      throw new YWServiceException(Status.BAD_REQUEST, "Certificate not present: " + certUUID);
    }

    if (cert.certType != CertificateInfo.Type.CustomCertHostPath
        || rootCert.certType != CertificateInfo.Type.CustomCertHostPath) {
      throw new YWServiceException(Status.BAD_REQUEST, "Only custom certs can be rotated.");
    }

    if (rootCA.equals(certUUID)) {
      throw new YWServiceException(Status.BAD_REQUEST, "Cluster already has the same cert.");
    }

    if (!rotateRoot && CertificateHelper.areCertsDiff(rootCA, certUUID)) {
      throw new YWServiceException(Status.BAD_REQUEST, "CA certificates cannot be different.");
    }

    if (CertificateHelper.arePathsSame(rootCA, certUUID)) {
      throw new YWServiceException(Status.BAD_REQUEST, "The node cert/key paths cannot be same.");
    }
  }

  public static class Converter extends BaseConverter<CertsRotateParams> {}
}
