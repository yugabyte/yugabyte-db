// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import io.ebean.DB;
import io.ebean.SqlRow;
import io.ebean.SqlUpdate;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.List;
import java.util.StringJoiner;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V303__Remove_Dangling_Provider extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    DB.execute(V303__Remove_Dangling_Provider::removeDanglingProvider);
  }

  public static void removeDanglingProvider() {
    String customerGet = "SELECT distinct uuid from customer";
    List<SqlRow> customerResultList = DB.sqlQuery(customerGet).findList();
    List<String> customerUUIDs = new ArrayList<>();
    for (SqlRow row : customerResultList) {
      String customerUUID = row.getString("uuid");
      customerUUIDs.add(customerUUID);
    }

    String providerGet = "SELECT uuid, customer_uuid from provider";
    List<SqlRow> providerResultList = DB.sqlQuery(providerGet).findList();

    List<String> danglingProviderUUIDs = new ArrayList<>();
    for (SqlRow row : providerResultList) {
      String providerCustomerUUID = row.getString("customer_uuid");
      if (!customerUUIDs.contains(providerCustomerUUID)) {
        String providerUUID = row.getString("uuid");
        danglingProviderUUIDs.add(providerUUID);
      }
    }
    if (danglingProviderUUIDs.isEmpty()) {
      return;
    }

    StringJoiner uuidList = new StringJoiner(",", "(", ")");
    for (String uuid : danglingProviderUUIDs) {
      uuidList.add("'" + uuid + "'");
    }
    log.debug(
        "Deleting the following providers {} as corresponding customer is deleted",
        uuidList.toString());

    List<String> danglingRegionUUIDs = new ArrayList<>();
    String regionGet =
        "SELECT distinct uuid from region where provider_uuid IN " + uuidList.toString();
    List<SqlRow> regionsResultList = DB.sqlQuery(regionGet).findList();
    for (SqlRow row : regionsResultList) {
      String regionUUID = row.getString("uuid");
      danglingRegionUUIDs.add(regionUUID);
    }

    SqlUpdate sqlUpdate;
    int rowsDeleted = 0;
    if (danglingRegionUUIDs.size() > 0) {
      StringJoiner regionUuidList = new StringJoiner(",", "(", ")");
      for (String uuid : danglingRegionUUIDs) {
        regionUuidList.add("'" + uuid + "'");
      }
      log.debug(
          "Deleting the following regions {} as these are associated with dangling providers",
          regionUuidList.toString());

      String availabilityZoneDelete =
          "DELETE from availability_zone where region_uuid IN " + regionUuidList.toString();
      sqlUpdate = DB.sqlUpdate(availabilityZoneDelete);
      rowsDeleted = DB.getDefault().execute(sqlUpdate);
      log.debug("Deleted {} az's", rowsDeleted);

      String regionDelete = "DELETE from region where provider_uuid IN " + uuidList.toString();
      sqlUpdate = DB.sqlUpdate(regionDelete);
      rowsDeleted = DB.getDefault().execute(sqlUpdate);
      log.debug("Deleted {} regions", rowsDeleted);
    }

    String imageBundleDelete =
        "DELETE from image_bundle where provider_uuid IN " + uuidList.toString();
    sqlUpdate = DB.sqlUpdate(imageBundleDelete);
    rowsDeleted = DB.getDefault().execute(sqlUpdate);
    log.debug("Deleted {} imageBundles", rowsDeleted);

    String providerDelete = "DELETE from provider where uuid IN " + uuidList.toString();
    sqlUpdate = DB.sqlUpdate(providerDelete);
    rowsDeleted = DB.getDefault().execute(sqlUpdate);
    log.debug("Deleted {} providers", rowsDeleted);
  }
}
