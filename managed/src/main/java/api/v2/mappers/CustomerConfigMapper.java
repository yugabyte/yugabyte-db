// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.CustomerConfig;
import api.v2.models.CustomerConfigInfo;
import api.v2.models.CustomerConfigSpec;
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

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  CustomerConfig toCustomerConfig(com.yugabyte.yw.models.configs.CustomerConfig source);

  @Mapping(target = "type", source = "type", qualifiedByName = "typeToEnum")
  @Mapping(target = "data", source = ".", qualifiedByName = "customerConfigDataToMap")
  CustomerConfigSpec toCustomerConfigSpec(com.yugabyte.yw.models.configs.CustomerConfig source);

  @Mapping(target = "uuid", source = "configUUID")
  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "state", source = "state", qualifiedByName = "stateToEnum")
  @Mapping(
      target = "isKubernetesOperatorControlled",
      expression = "java(source.isKubernetesOperatorControlled())")
  CustomerConfigInfo toCustomerConfigInfo(com.yugabyte.yw.models.configs.CustomerConfig source);

  @Named("typeToEnum")
  default CustomerConfigSpec.TypeEnum typeToEnum(
      com.yugabyte.yw.models.configs.CustomerConfig.ConfigType type) {
    if (type == null) {
      return null;
    }
    return CustomerConfigSpec.TypeEnum.fromValue(type.name());
  }

  @Named("stateToEnum")
  default CustomerConfigInfo.StateEnum stateToEnum(
      com.yugabyte.yw.models.configs.CustomerConfig.ConfigState state) {
    if (state == null) {
      return null;
    }
    return CustomerConfigInfo.StateEnum.fromValue(state.name());
  }

  @Named("customerConfigDataToMap")
  default Map<String, Object> customerConfigDataToMap(
      com.yugabyte.yw.models.configs.CustomerConfig config) {
    if (config.getData() == null) {
      return null;
    }
    return Json.mapper().convertValue(config.getMaskedData(), new TypeReference<>() {});
  }
}
