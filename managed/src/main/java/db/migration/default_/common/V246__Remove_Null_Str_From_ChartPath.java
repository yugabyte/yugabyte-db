// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.Iterator;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

public class V246__Remove_Null_Str_From_ChartPath extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    ResultSet softwareReleasesResult =
        connection
            .createStatement()
            .executeQuery("SELECT value FROM yugaware_property WHERE name = 'SoftwareReleases'");
    while (softwareReleasesResult.next()) {
      String softwareReleasesString = softwareReleasesResult.getString("value");
      if (softwareReleasesString != null) {
        JsonNode softwareReleasesJsonNode = Json.parse(softwareReleasesString);
        if (softwareReleasesJsonNode.isObject()) {
          ObjectNode updatedSoftwareReleases = Json.newObject();
          ObjectNode softwareReleases = (ObjectNode) softwareReleasesJsonNode;
          Iterator<Entry<String, JsonNode>> releaseIterator = softwareReleases.fields();
          while (releaseIterator.hasNext()) {
            Entry<String, JsonNode> release = releaseIterator.next();
            String version = release.getKey();
            if (release.getValue().isObject()) {
              ObjectNode releaseObject = (ObjectNode) release.getValue();
              Optional.ofNullable(releaseObject.get("chartPath"))
                  .ifPresent(
                      chartPath -> {
                        if (Objects.equals(chartPath.asText(), "null")) {
                          releaseObject.remove("chartPath");
                        }
                      });
              updatedSoftwareReleases.set(version, releaseObject);
            } else {
              updatedSoftwareReleases.set(version, release.getValue());
            }
          }

          PreparedStatement updateSoftwareReleasesStmt =
              connection.prepareStatement("UPDATE yugaware_property SET value = ? WHERE name = ?");
          updateSoftwareReleasesStmt.setString(1, Json.stringify(updatedSoftwareReleases));
          updateSoftwareReleasesStmt.setString(2, "SoftwareReleases");
          updateSoftwareReleasesStmt.executeUpdate();
          updateSoftwareReleasesStmt.close();
        }
      }
    }
    softwareReleasesResult.close();
  }
}
