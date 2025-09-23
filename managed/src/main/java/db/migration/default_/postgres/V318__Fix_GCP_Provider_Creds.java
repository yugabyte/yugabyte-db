// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V318__Fix_GCP_Provider_Creds extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    migrate(context.getConnection());
  }

  static void migrate(Connection connection) throws SQLException {
    PreparedStatement updateProviderDetailsStmt =
        connection.prepareStatement(
            "UPDATE provider set details=pgp_sym_encrypt(?, 'provider::details') where uuid = ?");
    ResultSet providerRs =
        connection
            .createStatement()
            .executeQuery(
                "SELECT pgp_sym_decrypt(details, 'provider::details') as data, uuid from provider"
                    + " where code='gcp'");
    while (providerRs.next()) {
      UUID providerUUID = UUID.fromString(providerRs.getString("uuid"));
      byte[] providerDetailsByte = providerRs.getBytes("data");
      ObjectMapper objectMapper = Json.mapper();
      try {
        JsonNode details = objectMapper.readTree(providerDetailsByte);
        if (details != null) {
          if (details.has("cloudInfo")) {
            JsonNode cloudInfo = details.get("cloudInfo");
            if (cloudInfo.has("gcp")) {
              JsonNode gcpCloudInfo = cloudInfo.get("gcp");
              JsonNode gceCredentials = gcpCloudInfo.get("gceApplicationCredentials");
              if (!gceCredentials.isTextual()) {
                ((ObjectNode) gcpCloudInfo)
                    .put(
                        "gceApplicationCredentials",
                        objectMapper.writeValueAsString(gceCredentials));
                ((ObjectNode) cloudInfo).put("gcp", gcpCloudInfo);
              }
            }
            ((ObjectNode) details).put("cloudInfo", cloudInfo);
          }
        }
        log.info("Fixing credentials for gcp Provider {}", providerUUID);
        updateProviderDetailsStmt.setString(1, Json.stringify(details));
        updateProviderDetailsStmt.setObject(2, providerUUID);
        updateProviderDetailsStmt.executeUpdate();
      } catch (Exception e) {
        log.error(e.getLocalizedMessage());
      }
    }
    updateProviderDetailsStmt.close();
  }
}
