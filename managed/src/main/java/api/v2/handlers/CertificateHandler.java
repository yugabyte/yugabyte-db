// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import api.v2.models.Certificate;
import api.v2.models.CertificateSpec;
import api.v2.models.CertificateType;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import io.ebean.DuplicateKeyException;
import java.security.GeneralSecurityException;
import java.security.KeyPair;
import java.security.cert.X509Certificate;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.UUID;

@Singleton
public class CertificateHandler {

  private final RuntimeConfGetter confGetter;
  private final CertificateHelper certificateHelper;

  @Inject
  public CertificateHandler(RuntimeConfGetter confGetter, CertificateHelper certificateHelper) {
    this.confGetter = confGetter;
    this.certificateHelper = certificateHelper;
  }

  public Certificate createCertificate(UUID cUUID, CertificateSpec spec) {
    // Authorization has already checked that the customer exists and is the caller's.
    Customer customer = Customer.get(cUUID);
    String label = spec.getLabel();
    if (CertificateInfo.get(cUUID, label) != null) {
      throw labelConflict(label);
    }
    boolean upload = spec.getCertContent() != null;
    if (upload != (spec.getKeyContent() != null)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Give both cert_content and key_content, or neither to have YBA generate the CA");
    }
    if (upload && spec.getLifetimeYears() != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "lifetime_years applies only to a CA that YBA generates");
    }
    String certContent;
    String keyContent;
    if (upload) {
      // v1 rejects content without a trailing newline; PEM parsing doesn't need one.
      certContent =
          spec.getCertContent().endsWith("\n")
              ? spec.getCertContent()
              : spec.getCertContent() + "\n";
      keyContent = spec.getKeyContent();
    } else {
      // Generated here, so that the CA's key never leaves YBA. Stored like an uploaded CA below.
      int lifetimeYears =
          spec.getLifetimeYears() != null
              ? spec.getLifetimeYears()
              : confGetter.getStaticConf().getInt("yb.tlsCertificate.root.expiryInYears");
      KeyPair keyPair;
      try {
        keyPair = CertificateHelper.getKeyPairObject();
      } catch (GeneralSecurityException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Unable to generate the CA key: " + e.getMessage());
      }
      X509Certificate caCert =
          certificateHelper.generateCACertificate(label, keyPair, lifetimeYears);
      certContent = CertificateHelper.getAsPemString(caCert);
      keyContent = CertificateHelper.getAsPemString(keyPair.getPrivate());
    }
    UUID certUUID;
    try {
      certUUID =
          CertificateHelper.uploadRootCA(
              label,
              cUUID,
              AppConfigHelper.getStoragePath(),
              certContent,
              keyContent,
              CertConfigType.valueOf(spec.getCertType().toString()),
              null /* customCertInfo */,
              null /* customServerCertData */,
              confGetter.getConfForScope(customer, CustomerConfKeys.CheckCertificateConfig));
    } catch (DuplicateKeyException e) {
      // A concurrent request added the same label after the check above.
      throw labelConflict(label);
    }
    CertificateInfo cert = CertificateInfo.get(certUUID);
    return new Certificate()
        .uuid(cert.getUuid())
        .label(cert.getLabel())
        .certType(CertificateType.fromValue(cert.getCertType().name()))
        .startDate(toOffsetDateTime(cert.getStartDateIso()))
        .expiryDate(toOffsetDateTime(cert.getExpiryDateIso()));
  }

  private static PlatformServiceException labelConflict(String label) {
    return new PlatformServiceException(
        CONFLICT, String.format("Certificate with label %s already exists", label));
  }

  private static OffsetDateTime toOffsetDateTime(Date date) {
    return date.toInstant().atOffset(ZoneOffset.UTC);
  }
}
