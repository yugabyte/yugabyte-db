// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.Task;
import api.v2.models.TaskDetails;
import api.v2.models.TaskInfo;
import api.v2.models.TaskSubtaskGroupDetails;
import api.v2.models.TaskVersionNumbers;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.forms.CustomerTaskFormData;
import java.util.List;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

@Mapper(config = CentralConfig.class, uses = DateTimeMapper.class)
public interface TaskMapper {

  TaskMapper INSTANCE = Mappers.getMapper(TaskMapper.class);

  @Mapping(target = "info", source = ".")
  Task toTask(CustomerTaskFormData source);

  @Mapping(target = "uuid", source = "id")
  @Mapping(target = "targetUuid", source = "targetUUID")
  @Mapping(target = "details", ignore = true)
  TaskInfo toTaskInfo(CustomerTaskFormData source);

  @AfterMapping
  default void mapDetails(@MappingTarget TaskInfo target, CustomerTaskFormData source) {
    target.setDetails(toTaskDetails(source.details));
  }

  private boolean hasRawJson(JsonNode node, String key) {
    return node.has(key) && !node.get(key).isNull();
  }

  /**
   * Maps internal commissioner task-status JSON (camelCase keys) to v2 {@link TaskDetails}
   * (snake_case API fields with {@code @JsonAlias} for deserialization).
   */
  default TaskDetails toTaskDetails(JsonNode node) {
    if (node == null || node.isNull()) {
      return null;
    }

    TaskDetails details = new TaskDetails();

    if (hasRawJson(node, "taskDetails")) {
      List<TaskSubtaskGroupDetails> groups =
          Json.mapper().convertValue(node.get("taskDetails"), new TypeReference<>() {});
      details.setTaskDetails(groups);
    }

    if (hasRawJson(node, "versionNumbers")) {
      details.setVersionNumbers(
          Json.mapper().convertValue(node.get("versionNumbers"), TaskVersionNumbers.class));
    }

    if (hasRawJson(node, "softwareUpgradeProgress")) {
      details.setSoftwareUpgradeProgress(
          Json.mapper()
              .convertValue(
                  node.get("softwareUpgradeProgress"),
                  api.v2.models.SoftwareUpgradeProgress.class));
    }

    if (hasRawJson(node, "auditLogConfig")) {
      details.setAuditLogConfig(
          Json.mapper()
              .convertValue(node.get("auditLogConfig"), api.v2.models.AuditLogConfig.class));
    }

    if (hasRawJson(node, "queryLogConfig")) {
      details.setQueryLogConfig(
          Json.mapper()
              .convertValue(node.get("queryLogConfig"), api.v2.models.QueryLogConfig.class));
    }

    if (hasRawJson(node, "metricsExportConfig")) {
      details.setMetricsExportConfig(
          Json.mapper()
              .convertValue(
                  node.get("metricsExportConfig"), api.v2.models.MetricsExportConfig.class));
    }

    return details;
  }
}
