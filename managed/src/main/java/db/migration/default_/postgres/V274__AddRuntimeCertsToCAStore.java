// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigList;
import com.typesafe.config.ConfigValue;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Timestamp;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V274__AddRuntimeCertsToCAStore extends BaseJavaMigration {

  private final String HA_CONF_PATH = "yb.ha.ws";
  private final String WEBHOOK_CONF_PATH = "yb.alert.webhook.ws";
  private final String SLACK_CONF_PATH = "yb.alert.slack.ws";
  private final String PAGERDUTY_CONF_PATH = "yb.alert.pagerduty.ws";

  private final List<String> WS_CONF_PATH =
      Arrays.asList(HA_CONF_PATH, WEBHOOK_CONF_PATH, SLACK_CONF_PATH, PAGERDUTY_CONF_PATH);

  private final String PEM_STORE_FILE_NAME = "yb-ca-bundle.pem";
  private final String PKCS12_STORE_FILE_NAME = "ybPkcs12CaCerts";

  String storagePath = AppConfigHelper.getStoragePath();
  String trustHome = String.format("%s/certs/trust-store", storagePath);
  String pemStorePath = String.format("%s/%s", trustHome, PEM_STORE_FILE_NAME);
  String pemFilePath = String.format("/certs/trust-store/%s", PEM_STORE_FILE_NAME);

  String pkcs12StorePath = String.format("%s/%s", trustHome, PKCS12_STORE_FILE_NAME);
  String pkcs12FilePath = String.format("/certs/trust-store/%s", PKCS12_STORE_FILE_NAME);
  char[] trustStorePassword = "global-truststore-password".toCharArray();

  @Override
  public void migrate(Context context) throws Exception {
    for (String confPath : WS_CONF_PATH) {
      migrate_conf(context, confPath);
    }
  }

  public void migrate_conf(Context context, String confPath) throws Exception {
    String certName = String.format("%s-CA", confPath.split("[.]", 2)[1]);
    Connection connection = context.getConnection();
    String confQuery =
        String.format(
            "select convert_from(value, 'UTF-8') as value from runtime_config_entry "
                + "where path='%s'",
            confPath);
    ResultSet confCertEntry = connection.createStatement().executeQuery(confQuery);
    if (confCertEntry.next()) {
      String wsConf = confCertEntry.getString("value");
      Config conf = ConfigFactory.parseString(wsConf).resolve();
      if (!conf.hasPath("ssl.trustManager.stores")) {
        log.debug("No stores list defined for {}. Nothing to transfer.", confPath);
        return;
      }
      ConfigList sslStoreList = conf.getList("ssl.trustManager.stores");
      List<String> allCerts = new ArrayList<>();
      for (ConfigValue certEntry : sslStoreList) {
        Object certEntryMap = certEntry.unwrapped();
        Map<String, String> certMap = (Map<String, String>) certEntryMap;
        allCerts.add(certMap.get("data"));
      }
      log.debug("All the certs from {}: {}\n", confPath, allCerts);

      if (!allCerts.isEmpty()) {
        // Create empty PEM store if it doesn't exist.
        boolean doesPemStoreExist = new File(pemStorePath).exists();
        if (!doesPemStoreExist) {
          File sysTrustStoreFile = new File(pemStorePath);
          sysTrustStoreFile.getParentFile().mkdirs();
          sysTrustStoreFile.createNewFile();
          log.debug("Created an empty YBA PEM trust-store");
        }
        // Create empty Pkcs12 store if it doesn't exist.
        boolean doesPkcs12StoreExist = new File(pkcs12StorePath).exists();
        if (!doesPkcs12StoreExist) {
          File trustStoreFile = new File(pkcs12StorePath);
          trustStoreFile.getParentFile().mkdirs();
          trustStoreFile.createNewFile();
          log.debug("Created an empty YBA pkcs12 trust-store");
        }
        KeyStore ybaJavaStore = getTrustStore(trustStorePassword);

        if (addCAsToTruststore(connection, allCerts, certName, ybaJavaStore)) {
          log.debug("Added {} configuration's CA(s) to YBA's trust store", confPath);
        }
      }
    } else {
      log.info("Configuration {} does not exist, nothing to do.", confPath);
    }
  }

  public boolean addCAsToTruststore(
      Connection connection, List<String> allCerts, String certName, KeyStore ybaJavaStore)
      throws Exception {
    String customerQuery = "select uuid from customer order by id limit 1";
    ResultSet customerResult = connection.createStatement().executeQuery(customerQuery);
    UUID customerUuid = null;
    if (customerResult.next()) {
      customerUuid = UUID.fromString(customerResult.getString("uuid"));
    }
    X509Certificate x509Cert = null;

    int numCerts = allCerts.size();
    for (int count = 0; count < numCerts; count++) {
      try {
        x509Cert = CertificateHelper.convertStringToX509Cert(allCerts.get(count));
      } catch (CertificateException e) {
        log.warn("Certificate exception occurred, ignoring it.", e);
        return false;
      }

      UUID certUuid = UUID.randomUUID();
      Date createDate = new Timestamp(new Date().getTime());
      Date startDate = x509Cert.getNotBefore();
      Date expiryDate = x509Cert.getNotAfter();
      String certPath = String.format("%s/certs/trust-store/%s/ca.crt", storagePath, certUuid);
      File certFile = new File(certPath);
      certFile.getParentFile().mkdirs();
      // Create file data in certPath, using syncToDb as false as it has runtime config use.
      CertificateHelper.writeCertFileContentToCertPath(x509Cert, certPath, false);
      String filePath = String.format("/certs/trust-store/%s/ca.crt", certUuid);
      String encodedCert = Base64.getEncoder().encodeToString(allCerts.get(count).getBytes());
      String fileCreateSql =
          String.format(
              "INSERT into file_data(file_path, extension, parent_uuid, file_content, "
                  + "timestamp) VALUES ('%s', 'crt', '%s', '%s', '%s') ",
              filePath, certUuid, encodedCert, createDate);
      connection.createStatement().executeUpdate(fileCreateSql);
      log.debug("Created CA certificate {} in trust-store and in the DB", certUuid);

      if (numCerts > 1) {
        certName = certName + "-" + count;
      }
      String createSql =
          String.format(
              "INSERT into custom_ca_certificate_info(id, customer_id, name, start_date, "
                  + "expiry_date, created_time, contents, active) VALUES "
                  + "('%s', '%s', '%s', '%s', '%s', '%s', '%s', 'true')",
              certUuid, customerUuid, certName, startDate, expiryDate, createDate, certPath);
      connection.createStatement().executeUpdate(createSql);

      // Add the CA in the Pkcs12 format truststore.
      ybaJavaStore.setCertificateEntry(certName, x509Cert);

      // Add the PEM store file in the file-system.
      CertificateHelper.writeCertFileContentToCertPath(x509Cert, pemStorePath, false, true);
      log.debug("Truststore '{}' now has the cert {}", pemStorePath, certName);
    }

    // Save pkcs12 store in FileSystem.
    FileOutputStream storeOutputStream = new FileOutputStream(pkcs12StorePath);
    ybaJavaStore.store(storeOutputStream, trustStorePassword);
    log.debug("Pkcs12 truststore written to {}", pkcs12StorePath);

    Date createDate = new Timestamp(new Date().getTime());
    // Add the PEM store file to the DB.

    String allCertsStr = String.join(System.lineSeparator(), allCerts);
    String allCertsEncoded = Base64.getEncoder().encodeToString(allCertsStr.getBytes());
    String createPem =
        String.format(
            "INSERT into file_data(file_path, extension, parent_uuid, file_content, "
                + "timestamp) VALUES ('%s', 'pem', NULL, '%s', '%s') ",
            pemFilePath, allCertsEncoded, createDate);
    connection.createStatement().executeUpdate(createPem);
    log.debug("Create PEM certificate bundle {} in the DB", pemFilePath);

    // Add the Pkcs12 store file in the DB.
    String pkcs12Data =
        Base64.getEncoder()
            .encodeToString((Files.readAllBytes(new File(pkcs12StorePath).toPath())));
    String createPkcs12 =
        String.format(
            "INSERT into file_data(file_path, extension, parent_uuid, file_content, "
                + "timestamp) VALUES ('%s', '', NULL, '%s', '%s') ",
            pkcs12FilePath, pkcs12Data, createDate);
    connection.createStatement().executeUpdate(createPkcs12);
    log.debug("Create PKCS12 certificate bundle {} in the DB", pkcs12FilePath);

    return true;
  }

  protected KeyStore getTrustStore(char[] trustStorePassword) throws Exception {
    KeyStore trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
    trustStore.load(null, trustStorePassword);
    return trustStore;
  }
}
