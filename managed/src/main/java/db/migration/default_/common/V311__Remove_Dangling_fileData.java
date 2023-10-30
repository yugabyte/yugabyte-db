package db.migration.default_.common;

import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.models.FileData;
import io.ebean.DB;
import io.ebean.Ebean;
import io.ebean.SqlRow;
import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V311__Remove_Dangling_fileData extends BaseJavaMigration {

  private static final Pattern UUID_REGEX =
      Pattern.compile(
          "[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}");

  @Override
  public void migrate(Context context) throws Exception {
    DB.execute(V311__Remove_Dangling_fileData::removeDanglingFileData);
  }

  public static void removeDanglingFileData() {
    // Existing provider(s) on DB
    String providerGet = "SELECT uuid from provider";
    List<SqlRow> providerResultList = Ebean.createSqlQuery(providerGet).findList();
    List<String> providerUUIDs = new ArrayList<>();
    for (SqlRow row : providerResultList) {
      String providerUUID = row.getString("uuid");
      providerUUIDs.add(providerUUID);
    }

    // Existing customer(s) on DB
    String customerGet = "SELECT uuid from customer";
    List<SqlRow> customerResultList = Ebean.createSqlQuery(customerGet).findList();
    List<String> customerUUIDs = new ArrayList<>();
    for (SqlRow row : customerResultList) {
      String customerUUID = row.getString("uuid");
      customerUUIDs.add(customerUUID);
    }

    // Existing universe(s) on DB
    String UniverseGet = "SELECT name from universe";
    List<SqlRow> universeResultList = Ebean.createSqlQuery(UniverseGet).findList();
    List<String> universeNames = new ArrayList<>();
    for (SqlRow row : universeResultList) {
      String universeName = row.getString("name");
      universeNames.add(universeName);
    }

    String CertsGet = "Select uuid, customer_uuid from certificate_info;";
    List<SqlRow> certsResultList = Ebean.createSqlQuery(CertsGet).findList();
    Map<String, List<String>> customerCerts = new HashMap<>();
    for (SqlRow row : certsResultList) {
      String customerUUID = row.getString("customer_uuid");
      String certUUID = row.getString("uuid");
      if (customerCerts.containsKey(customerUUID)) {
        customerCerts.get(customerUUID).add(certUUID);
      } else {
        List<String> newCertList = new ArrayList<>();
        newCertList.add(certUUID);
        customerCerts.put(customerUUID, newCertList);
      }
    }

    clearDiskFiles("/keys", providerUUIDs);
    clearDiskFiles("/licenses", customerUUIDs);
    clearDiskFiles("/certs", customerUUIDs);
    clearDiskFiles("/node-agent/certs", customerUUIDs);

    for (String customerUUID : customerCerts.keySet()) {
      clearDiskFiles("/certs/" + customerUUID, customerCerts.get(customerUUID));
    }
  }

  public static void clearDiskFiles(String child, List<String> resourceUUIDs) {
    File directory = new File(AppConfigHelper.getStoragePath(), child);
    if (directory.exists() && directory.isDirectory()) {
      File[] files = directory.listFiles();
      if (files != null) {
        for (File file : files) {
          // To avoid deletion of the trust-store directory for certs
          Matcher matcher = UUID_REGEX.matcher(file.getName());
          if (!resourceUUIDs.contains(file.getName()) && matcher.matches()) {
            log.debug(
                "Deleting {} on disk as corresponding provider/customer is deleted",
                file.getAbsolutePath());
            FileData.deleteFiles(file.getAbsolutePath(), true);
          }
        }
      }
    }
  }
}
