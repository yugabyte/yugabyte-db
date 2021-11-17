// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.nio.charset.Charset;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class UniverseUpdateRootCert extends UniverseTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(UniverseUpdateRootCert.class);
  public static String MULTI_ROOT_CERT = "%s.ca.multi.root.crt";

  public enum UpdateRootCertAction {
    MultiCert,
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
      UniverseDefinitionTaskParams universeDetails =
          Universe.get(taskParams().universeUUID).getUniverseDetails();
      if (taskParams().action == UpdateRootCertAction.MultiCert) {
        if (taskParams().rootCA != null
            && universeDetails.rootCA != null
            && !universeDetails.rootCA.equals(taskParams().rootCA)) {
          CertificateInfo oldRootCert = CertificateInfo.get(universeDetails.rootCA);
          CertificateInfo newRootCert = CertificateInfo.get(taskParams().rootCA);
          if (!oldRootCert.checksum.equals(newRootCert.checksum)) {
            // Create a new file in the same directory as current root cert
            // Append the file with contents of both old and new root certs
            File oldRootCertFile = new File(oldRootCert.certificate);
            File newRootCertFile = new File(newRootCert.certificate);
            File multiCertFile =
                new File(
                    oldRootCertFile.getParent()
                        + File.separator
                        + String.format(MULTI_ROOT_CERT, universeDetails.rootCA));
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
                    oldRootCert.label + " (TEMPORARY)",
                    multiCertFile.getAbsolutePath());
            Universe.saveDetails(
                taskParams().universeUUID,
                universe -> {
                  UniverseDefinitionTaskParams details = universe.getUniverseDetails();
                  details.rootCA = temporaryCert.uuid;
                  universe.setUniverseDetails(details);
                });
          }
        }
      } else if (taskParams().action == UpdateRootCertAction.Reset) {
        // Update the root cert to point to the original root cert
        // Delete the temporary multi root cert file
        if (universeDetails.rootCA != null) {
          CertificateInfo rootCert = CertificateInfo.get(universeDetails.rootCA);
          if (CertificateInfo.isTemporary(rootCert)) {
            File rootCertFile = new File(rootCert.certificate);
            // Temporary root cert file follows the below convention
            // <original-root-ca-uuid>.ca.multi.root.crt
            // Original rootCA uuid is extracted from the file name
            UUID originalRootCA = UUID.fromString(rootCertFile.getName().split("\\.")[0]);
            Universe.saveDetails(
                taskParams().universeUUID,
                universe -> {
                  UniverseDefinitionTaskParams details = universe.getUniverseDetails();
                  details.rootCA = originalRootCA;
                  universe.setUniverseDetails(details);
                });
            rootCertFile.delete();
            rootCert.delete();
          }
        }
      }
    } catch (Exception e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.warn(msg, e.getMessage());
      throw new RuntimeException(msg, e);
    }
  }
}
