// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.CONFLICT;

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
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.UUID;

@Singleton
public class CertificateHandler {

  private final RuntimeConfGetter confGetter;

  @Inject
  public CertificateHandler(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public Certificate createCertificate(UUID cUUID, CertificateSpec spec) {
    // Authorization has already checked that the customer exists and is the caller's.
    Customer customer = Customer.get(cUUID);
    String label = spec.getLabel();
    if (CertificateInfo.get(cUUID, label) != null) {
      throw labelConflict(label);
    }
    // v1 rejects content without a trailing newline; PEM parsing doesn't need one.
    String certContent =
        spec.getCertContent().endsWith("\n") ? spec.getCertContent() : spec.getCertContent() + "\n";
    UUID certUUID;
    try {
      certUUID =
          CertificateHelper.uploadRootCA(
              label,
              cUUID,
              AppConfigHelper.getStoragePath(),
              certContent,
              spec.getKeyContent(),
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
