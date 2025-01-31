// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import org.mapstruct.CollectionMappingStrategy;
import org.mapstruct.Context;
import org.mapstruct.MapperConfig;
import org.mapstruct.Mapping;
import org.mapstruct.MappingInheritanceStrategy;
import org.mapstruct.NullValuePropertyMappingStrategy;
import org.mapstruct.ReportingPolicy;
import play.mvc.Http.Request;

// Common config for all mappers is captured here
@MapperConfig(
    unmappedTargetPolicy = ReportingPolicy.IGNORE,
    mappingInheritanceStrategy = MappingInheritanceStrategy.EXPLICIT,
    collectionMappingStrategy = CollectionMappingStrategy.ADDER_PREFERRED,
    nullValuePropertyMappingStrategy = NullValuePropertyMappingStrategy.IGNORE)
public interface CentralConfig {

  /**
   * Ignore the following fields when mapping UniverseDefinitionTaskParams to any type inheriting
   * from UniverseDefinitionTaskParams, like UniverseConfigureTaskParams, GFlagsUpgradeParams, etc.
   */
  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  @Mapping(target = "allowInsecure", constant = "false")
  @Mapping(target = "capability", constant = "EDITS_ALLOWED")
  @Mapping(
      target = "creatingUser",
      expression = "java(com.yugabyte.yw.models.helpers.CommonUtils.getUserFromContext())")
  @Mapping(target = "platformUrl", expression = "java(request.host())")
  @Mapping(target = "useNewHelmNamingStyle", constant = "true")
  public UniverseDefinitionTaskParams defaultMapping(
      UniverseDefinitionTaskParams source, @Context Request request);
}
