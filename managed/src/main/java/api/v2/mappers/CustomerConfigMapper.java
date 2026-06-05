// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.CustomerConfig;
import com.fasterxml.jackson.core.type.TypeReference;
import java.util.Map;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.Named;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

@Mapper(config = CentralConfig.class, uses = UniverseDetailSubsetMapper.class)
public interface CustomerConfigMapper {

  CustomerConfigMapper INSTANCE = Mappers.getMapper(CustomerConfigMapper.class);

  @Mapping(target = "configUuid", source = "configUUID")
  @Mapping(target = "configName", source = "configName")
  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(
      target = "type",
      expression = "java(api.v2.models.CustomerConfig.TypeEnum.fromValue(source.getType().name()))")
  @Mapping(target = "name", source = "name")
  @Mapping(target = "data", source = ".", qualifiedByName = "customerConfigDataToMap")
  @Mapping(
      target = "state",
      expression =
          "java(api.v2.models.CustomerConfig.StateEnum.fromValue(source.getState().name()))")
  @Mapping(target = "isKubernetesOperatorControlled", source = "kubernetesOperatorControlled")
  CustomerConfig toCustomerConfig(com.yugabyte.yw.models.configs.CustomerConfig source);

  @Named("customerConfigDataToMap")
  default Map<String, Object> customerConfigDataToMap(
      com.yugabyte.yw.models.configs.CustomerConfig config) {
    return Json.mapper().convertValue(config.getData(), new TypeReference<>() {});
  }
}
