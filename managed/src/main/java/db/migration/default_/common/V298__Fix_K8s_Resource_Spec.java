package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.migrations.V298.Customer;
import com.yugabyte.yw.models.migrations.V298.Universe;
import io.ebean.DB;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

/*
 * V298 migration adds support for config
 */
@Slf4j
public class V298__Fix_K8s_Resource_Spec extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    try {
      DB.execute(V298__Fix_K8s_Resource_Spec::fixResourceSpec);
    } catch (Exception e) {
      log.error("Migration failed with exception", e);
    }
  }

  public static void fixResourceSpec() {
    ObjectMapper objectMapper = new ObjectMapper();
    for (Customer customer : Customer.getAll()) {
      for (Universe universe : Universe.getAllFromCustomer(customer)) {
        JsonNode rootNode = universe.getUniverseDetails();
        if (rootNode == null) {
          continue;
        }

        JsonNode clusters = rootNode.get("clusters");
        if (clusters.isArray()) {
          for (JsonNode cluster : clusters) {
            JsonNode userIntentNode = cluster.get("userIntent");
            if (userIntentNode != null) {
              String provider = userIntentNode.get("providerType").asText();
              String insType = userIntentNode.get("instanceType").asText();
              if (provider == null) {
                continue;
              }
              InstanceType instanceType = InstanceType.get(UUID.fromString(provider), insType);
              if (instanceType == null) {
                continue;
              }
              if (provider.equals("kubernetes") && instanceType != null) {
                // Create a new JSON object for masterK8SNodeResourceSpec
                ObjectNode masterK8SNodeResourceSpec = JsonNodeFactory.instance.objectNode();
                masterK8SNodeResourceSpec.put("memoryGib", instanceType.getMemSizeGB());
                masterK8SNodeResourceSpec.put("cpuCoreCount", instanceType.getNumCores());

                // Create a new JSON object for tserverK8SNodeResourceSpec
                ObjectNode tserverK8SNodeResourceSpec = JsonNodeFactory.instance.objectNode();
                tserverK8SNodeResourceSpec.put("memoryGib", instanceType.getMemSizeGB());
                tserverK8SNodeResourceSpec.put("cpuCoreCount", instanceType.getNumCores());

                // Add masterK8SNodeResourceSpec and tserverK8SNodeResourceSpec to the root JSON
                // object
                ((ObjectNode) userIntentNode)
                    .set("masterK8SNodeResourceSpec", masterK8SNodeResourceSpec);
                ((ObjectNode) userIntentNode)
                    .set("tserverK8SNodeResourceSpec", tserverK8SNodeResourceSpec);
              }
            }
          }
        }
        try {
          String updatedJsonString =
              objectMapper.writerWithDefaultPrettyPrinter().writeValueAsString(rootNode);
          universe.setUniverseDetails(updatedJsonString);
        } catch (JsonProcessingException e) {
          log.error("Exception in serializing json", e);
        }
      }
    }
  }
}
