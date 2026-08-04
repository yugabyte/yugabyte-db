package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
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
public class V289__Universe_Details_Arch_Update extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    ResultSet customerResult =
        connection.createStatement().executeQuery("SELECT uuid, id from customer");
    // Retrieve the customers.
    while (customerResult.next()) {
      String customerID = customerResult.getString("id");
      if (customerID.isEmpty()) {
        log.error(
            String.format("Customer ID missing for customer %s", customerResult.getString("uuid")));
        continue;
      }

      // Retrieve the universes associated with the customerID.
      PreparedStatement retrieveUniverseStatement =
          connection.prepareStatement(
              "SELECT universe_uuid, name, universe_details_json"
                  + " from universe where customer_id=?");
      retrieveUniverseStatement.setInt(1, Integer.parseInt(customerID));
      ResultSet univResultSet = retrieveUniverseStatement.executeQuery();
      PreparedStatement updateUniverseDetailsStmt =
          connection.prepareStatement(
              "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?");

      while (univResultSet.next()) {
        String universeDetailsString = univResultSet.getString("universe_details_json");
        String universeUUID = univResultSet.getString("universe_uuid");
        String universeName = univResultSet.getString("name");

        if (universeDetailsString != null) {
          try {
            JsonNode universeDetailsJson = Json.parse(universeDetailsString);
            // We will retrieve the imageBundle UUID from the primary cluster.
            // As we can't have read replicas & primary configured using different
            // arch types.
            String imageBundleUUID = retrieveImageBundleUUID(universeDetailsJson, connection);
            if (imageBundleUUID.isEmpty()) {
              log.debug(
                  String.format(
                      "Image Bundle is not associated with the universe %s, skipping",
                      universeUUID));
              continue;
            }

            String arch = retrieveImageBundleArch(imageBundleUUID, connection);
            if (arch.isEmpty()) {
              log.debug(
                  String.format(
                      "Architecture is not present for imageBundle %s, skipping...",
                      imageBundleUUID));
              continue;
            }

            ((ObjectNode) universeDetailsJson).put("arch", arch);
            updateUniverseDetailsStmt.setString(1, Json.stringify(universeDetailsJson));
            updateUniverseDetailsStmt.setObject(2, UUID.fromString(universeUUID));
            updateUniverseDetailsStmt.executeUpdate();
          } catch (Exception e) {
            log.error(
                String.format(
                    "Universe %s contains invalid details json, skipping..., %s",
                    universeUUID, e.getMessage()));
            continue;
          }
        }
      }
      retrieveUniverseStatement.close();
      updateUniverseDetailsStmt.close();
    }
    customerResult.close();
  }

  /*
   * Try retrieving the imageBundleUUID from the cluster, which should exist.
   * In case, it is not present this will try reading the default imageBundle
   * for the provider specified as part of the universe creation.
   */
  public String retrieveImageBundleUUID(JsonNode universeDetailsJson, Connection connection)
      throws SQLException {
    String imageBundleUUID = "";
    if (universeDetailsJson.has("clusters")) {
      ArrayNode clusters = (ArrayNode) universeDetailsJson.get("clusters");
      // Interested in the first cluster only.
      JsonNode cluster = clusters.get(0);
      if (!cluster.has("userIntent") || cluster.get("userIntent").isEmpty()) {
        log.debug(
            String.format(
                "Cluster %s exists without userIntent, can't continue",
                cluster.get("uuid").asText()));
        return "";
      }
      if (cluster.get("userIntent").get("imageBundleUUID") != null) {
        imageBundleUUID = cluster.get("userIntent").get("imageBundleUUID").asText();
      }
      if (imageBundleUUID.isEmpty()) {
        log.debug(
            "Image Bundle UUID is not present in the universe details trying with the provider");
        String providerUUID = cluster.get("userIntent").get("provider").asText();

        PreparedStatement retrieveImageBundle =
            connection.prepareStatement(
                "SELECT uuid from image_bundle where provider_uuid=? AND is_default='true'");
        retrieveImageBundle.setObject(1, UUID.fromString(providerUUID));
        ResultSet imageBundleSet = retrieveImageBundle.executeQuery();

        while (imageBundleSet.next()) {
          // There will be a single imageBundle per provider at the time this migration runs.
          imageBundleUUID = imageBundleSet.getString("uuid");
        }
        retrieveImageBundle.close();
      }
    }
    return imageBundleUUID;
  }

  public String retrieveImageBundleArch(String imageBundleUUID, Connection connection)
      throws SQLException {
    String arch = "";
    PreparedStatement retrieveImageBundleStatement =
        connection.prepareStatement("SELECT uuid, name, details from image_bundle where uuid=?");
    retrieveImageBundleStatement.setObject(1, UUID.fromString(imageBundleUUID));
    ResultSet imageBundleResultSet = retrieveImageBundleStatement.executeQuery();

    while (imageBundleResultSet.next()) {
      String detailsJson = imageBundleResultSet.getString("details");
      String name = imageBundleResultSet.getString("name");
      try {
        JsonNode imageBundleDetails = Json.parse(detailsJson);
        arch = imageBundleDetails.get("arch").asText();
      } catch (Exception e) {
        log.error(String.format("ImageBundle %s contains invalid details json, skipping...", name));
      }
    }
    imageBundleResultSet.close();

    return arch;
  }
}
