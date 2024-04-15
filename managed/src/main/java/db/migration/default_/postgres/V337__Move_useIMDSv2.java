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
public class V337__Move_useIMDSv2 extends BaseJavaMigration {
  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    ResultSet providerRs =
        connection
            .createStatement()
            .executeQuery(
                "SELECT pgp_sym_decrypt(details, 'provider::details') as data, uuid from provider"
                    + " where code='aws'");
    while (providerRs.next()) {
      UUID providerUUID = UUID.fromString(providerRs.getString("uuid"));
      byte[] providerDetailsByte = providerRs.getBytes("data");
      ObjectMapper objectMapper = Json.mapper();
      try {
        JsonNode details = objectMapper.readTree(providerDetailsByte);
        if (details != null) {
          if (details.has("cloudInfo")) {
            JsonNode cloudInfo = details.get("cloudInfo");
            if (cloudInfo.has("aws")) {
              JsonNode awsCloudInfo = cloudInfo.get("aws");
              if (awsCloudInfo.has("useIMDSv2")) {
                boolean useIMDSv2 = awsCloudInfo.get("useIMDSv2").asBoolean();
                if (useIMDSv2) {
                  String query =
                      String.format(
                          "select details,uuid from image_bundle where provider_uuid='%s'",
                          providerUUID.toString());
                  ResultSet imageBundleRs = connection.createStatement().executeQuery(query);
                  while (imageBundleRs.next()) {
                    // update image bundle details
                    UUID imageUUID = UUID.fromString(imageBundleRs.getString("uuid"));
                    JsonNode imageDetails =
                        objectMapper.readTree(imageBundleRs.getString("details"));
                    ((ObjectNode) imageDetails).put("useIMDSv2", "true");

                    PreparedStatement imageUpdate =
                        connection.prepareStatement(
                            "UPDATE image_bundle set details = ? where uuid = ?");
                    imageUpdate.setObject(1, imageDetails, java.sql.Types.OTHER);
                    imageUpdate.setObject(2, imageUUID);
                    imageUpdate.executeUpdate();
                    imageUpdate.closeOnCompletion();
                  }
                }

                // update provider details
                ((ObjectNode) awsCloudInfo).remove("useIMDSv2");
                ((ObjectNode) cloudInfo).set("aws", awsCloudInfo);
                ((ObjectNode) details).set("cloudInfo", cloudInfo);

                PreparedStatement providerUpdate =
                    connection.prepareStatement(
                        "UPDATE provider set details=pgp_sym_encrypt( ? , 'provider::details')"
                            + " where uuid = ? ");
                providerUpdate.setObject(1, details, java.sql.Types.OTHER);
                providerUpdate.setObject(2, providerUUID);
                providerUpdate.executeUpdate();
                providerUpdate.closeOnCompletion();
              }
            }
          }
        }
      } catch (Exception e) {
        log.error("Moving useIMDSv2 migration failed ", e);
      }
    }
  }
}
