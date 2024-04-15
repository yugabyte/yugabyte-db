// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import java.io.File;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Slf4j
public class UniverseUpdateRootCert extends UniverseTaskBase {

  // Format is <cert UUID>.<universe UUID>.<Suffix>.
  private static final String MULTI_ROOT_CERT = "%s.%s.ca.multi.root.crt";
  private static final String MULTI_ROOT_CERT_KEY = "%s.%s.ca.multi.root.key.pem";
  private static final String MULTI_ROOT_CERT_TMP_LABEL_SUFFIX = " (TEMPORARY)";

  @Inject
  protected UniverseUpdateRootCert(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public enum UpdateRootCertAction {
    MultiCert,
    MultiCertReverse,
    Reset
  }

  public static class Params extends UniverseTaskParams {
    public UUID rootCA;
    public UpdateRootCertAction action;
  }

  protected UniverseUpdateRootCert.Params taskParams() {
    return (UniverseUpdateRootCert.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      UniverseDefinitionTaskParams universeDetails = getUniverse().getUniverseDetails();
      if (taskParams().action == UpdateRootCertAction.MultiCert) {
        if (taskParams().rootCA != null
            && universeDetails.rootCA != null
            && !universeDetails.rootCA.equals(taskParams().rootCA)) {
          CertificateInfo oldRootCert = CertificateInfo.getOrBadRequest(universeDetails.rootCA);
          CertificateInfo newRootCert = CertificateInfo.getOrBadRequest(taskParams().rootCA);
          if (!CertificateInfo.isTemporary(oldRootCert)
              && !oldRootCert.getChecksum().equals(newRootCert.getChecksum())) {
            // It is not already a temporary multi cert and the certs are different.
            File oldRootCertFile = new File(oldRootCert.getCertificate());
            File newRootCertFile = new File(newRootCert.getCertificate());
            File multiCertFile =
                new File(
                    oldRootCertFile.getParent()
                        + File.separator
                        + String.format(
                            MULTI_ROOT_CERT,
                            universeDetails.rootCA,
                            universeDetails.getUniverseUUID()));
            String oldRootCertContent =
                FileUtils.readFileToString(oldRootCertFile, Charset.defaultCharset());
            String newRootCertContent =
                FileUtils.readFileToString(newRootCertFile, Charset.defaultCharset());
            FileUtils.write(multiCertFile, oldRootCertContent, Charset.defaultCharset());
            FileUtils.write(multiCertFile, newRootCertContent, Charset.defaultCharset(), true);
            // Create a temporary certificate pointing to the new multi certificate
            // Update universe details to temporarily point to the created certificate
            CertificateInfo temporaryCert =
                CertificateInfo.createCopy(
                    oldRootCert,
                    oldRootCert.getLabel() + MULTI_ROOT_CERT_TMP_LABEL_SUFFIX,
                    multiCertFile.getAbsolutePath());
            saveUniverseDetails(
                universe -> {
                  UniverseDefinitionTaskParams details = universe.getUniverseDetails();
                  details.rootCA = temporaryCert.getUuid();
                  universe.setUniverseDetails(details);
                });
          }
        }
      } else if (taskParams().action == UpdateRootCertAction.MultiCertReverse) {
        // Update the order of certs in temporary root cert created in previous step
        // Keep new cert first, followed by old cert
        // Add temp key file having the new root cert key
        if (taskParams().rootCA != null && universeDetails.rootCA != null) {
          CertificateInfo multiCert = CertificateInfo.get(universeDetails.rootCA);
          CertificateInfo newRootCert = CertificateInfo.get(taskParams().rootCA);
          // TODO why are these null checks needed if they must exist?
          if (newRootCert != null && multiCert != null && CertificateInfo.isTemporary(multiCert)) {
            File multiCertFile = new File(multiCert.getCertificate());
            // Temporary root cert file follows the below convention
            // <original-root-ca-uuid>.ca.multi.root.crt
            // Original rootCA uuid is extracted from the file name
            UUID oldRootCA = UUID.fromString(multiCertFile.getName().split("\\.")[0]);
            CertificateInfo oldRootCert = CertificateInfo.get(oldRootCA);

            File oldRootCertFile = new File(oldRootCert.getCertificate());
            File newRootCertFile = new File(newRootCert.getCertificate());
            String oldRootCertContent =
                FileUtils.readFileToString(oldRootCertFile, Charset.defaultCharset());
            String newRootCertContent =
                FileUtils.readFileToString(newRootCertFile, Charset.defaultCharset());
            FileUtils.write(multiCertFile, newRootCertContent, Charset.defaultCharset());
            FileUtils.write(multiCertFile, oldRootCertContent, Charset.defaultCharset(), true);

            if (newRootCert.getPrivateKey() != null) {
              File newCertKeyFile = new File(newRootCert.getPrivateKey());
              String newCertKeyContent =
                  FileUtils.readFileToString(newCertKeyFile, Charset.defaultCharset());
              File tempCertKeyFile =
                  new File(
                      oldRootCertFile.getParent()
                          + File.separator
                          + String.format(
                              MULTI_ROOT_CERT_KEY,
                              universeDetails.rootCA,
                              universeDetails.getUniverseUUID()));
              FileUtils.write(tempCertKeyFile, newCertKeyContent, Charset.defaultCharset());
              multiCert.setPrivateKey(tempCertKeyFile.getAbsolutePath());
            } else {
              multiCert.setPrivateKey(null);
            }

            // If certs rotation happening between different cert types:
            // SelfSigned -> HCVault or HCVault -> SelfSigned or HCVault -> HCVault
            // We should also update certConfigType and customCertInfo of multiCert
            // such that appropriate server certs are generated
            if (oldRootCert.getCustomCertInfo() != null
                || newRootCert.getCustomCertInfo() != null) {
              multiCert.setCertType(newRootCert.getCertType());
              multiCert.setCustomCertInfo(newRootCert.getCustomCertInfo());
            }

            multiCert.update();
          }
        }
      } else if (taskParams().action == UpdateRootCertAction.Reset) {
        // Update the root cert to point to the original root cert
        // Delete the temporary multi root cert file
        // Delete the temporary cert key file
        if (universeDetails.rootCA != null) {
          CertificateInfo rootCert = CertificateInfo.getOrBadRequest(universeDetails.rootCA);
          if (CertificateInfo.isTemporary(rootCert)) {
            File rootCertFile = new File(rootCert.getCertificate());
            // Temporary root cert file follows the below convention
            // <original-root-ca-uuid>.ca.multi.root.crt
            // Original rootCA uuid is extracted from the file name
            UUID originalRootCA = UUID.fromString(rootCertFile.getName().split("\\.")[0]);
            saveUniverseDetails(
                universe -> {
                  UniverseDefinitionTaskParams details = universe.getUniverseDetails();
                  details.rootCA =
                      taskParams().rootCA == null ? originalRootCA : taskParams().rootCA;
                  universe.setUniverseDetails(details);
                });
            Files.deleteIfExists(rootCertFile.toPath());
            if (rootCert.getPrivateKey() != null) {
              File rootCertKey = new File(rootCert.getPrivateKey());
              if (rootCertKey.getName().contains("ca.multi.root")) {
                Files.deleteIfExists(rootCertKey.toPath());
              }
            }
            rootCert.delete();
          }
        }
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
