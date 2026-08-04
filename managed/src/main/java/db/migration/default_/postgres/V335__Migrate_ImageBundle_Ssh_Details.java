// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.ImageBundleDetails;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V335__Migrate_ImageBundle_Ssh_Details extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws Exception {
    migrate(context.getConnection());
  }

  public void migrate(Connection connection) throws Exception {
    ResultSet providerRes =
        connection
            .createStatement()
            .executeQuery(
                "SELECT pgp_sym_decrypt(details, 'provider::details') as data, uuid, name, code"
                    + " from provider");
    PreparedStatement updateImageDetailsStmt =
        connection.prepareStatement("UPDATE image_bundle set details = ? where uuid = ?");
    while (providerRes.next()) {
      String providerUUID = providerRes.getString("uuid");
      byte[] providerDetailsByte = providerRes.getBytes("data");
      String providerCode = providerRes.getString("code");
      ObjectMapper objectMapper = Json.mapper();
      try {
        JsonNode providerDetails = objectMapper.readTree(providerDetailsByte);
        String query =
            String.format(
                "select details,uuid from image_bundle where provider_uuid='%s'", providerUUID);
        ResultSet imageBundleRs = connection.createStatement().executeQuery(query);
        while (imageBundleRs.next()) {
          // update image bundle details
          UUID imageBundleUUID = UUID.fromString(imageBundleRs.getString("uuid"));
          JsonNode imageDetails = objectMapper.readTree(imageBundleRs.getString("details"));
          ImageBundleDetails ibDetails = Json.fromJson(imageDetails, ImageBundleDetails.class);
          String sshUser = "";
          Integer sshPort = null;
          if (imageDetails != null) {
            if (providerCode.equals("aws")) {
              Map<String, ImageBundleDetails.BundleInfo> regions = ibDetails.getRegions();
              for (String region : regions.keySet()) {
                ImageBundleDetails.BundleInfo info = regions.get(region);
                sshUser = info.getSshUserOverride();
                sshPort = info.getSshPortOverride();
                ObjectNode regionsNode = (ObjectNode) ((ObjectNode) imageDetails).get("regions");
                ObjectNode regionNode = (ObjectNode) regionsNode.get(region);
                regionNode.set("sshUserOverride", null);
                regionNode.set("sshPortOverride", null);
                regionsNode.put(region, regionNode);
                ((ObjectNode) imageDetails).put("regions", regionsNode);
              }
            }
          }
          if (StringUtils.isBlank(sshUser)) {
            if (providerDetails.has("sshUser")) {
              sshUser = providerDetails.get("sshUser").asText();
            } else {
              sshUser = Common.CloudType.valueOf(providerCode).getSshUser();
            }
          }
          if (sshPort == null) {
            if (providerDetails.has("sshPort")) {
              sshPort = providerDetails.get("sshPort").asInt();
            } else {
              sshPort = 22;
            }
          }
          ((ObjectNode) imageDetails).put("sshUser", sshUser);
          ((ObjectNode) imageDetails).put("sshPort", sshPort);

          updateImageDetailsStmt.setObject(1, imageDetails, java.sql.Types.OTHER);
          updateImageDetailsStmt.setObject(2, imageBundleUUID);
          updateImageDetailsStmt.execute();
        }
      } catch (Exception e) {
        log.error("Migration V331 failed with error: ", e.getLocalizedMessage());
        throw e;
      }
    }
    updateImageDetailsStmt.close();
  }
}
