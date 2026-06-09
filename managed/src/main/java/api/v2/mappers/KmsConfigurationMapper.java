// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.KmsConfiguration;
import api.v2.models.KmsConfigurationMetadata;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.Collections;
import java.util.Map;
import java.util.stream.Collectors;
import org.mapstruct.AfterMapping;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.Named;
import org.mapstruct.factory.Mappers;
import play.libs.Json;

@Mapper(config = CentralConfig.class, uses = UniverseDetailSubsetMapper.class)
public interface KmsConfigurationMapper {

  KmsConfigurationMapper INSTANCE = Mappers.getMapper(KmsConfigurationMapper.class);

  @Mapping(
      target = "credentials",
      source = "authConfig",
      qualifiedByName = "maskedCredentialsFromAuth")
  @Mapping(target = "metadata", source = ".")
  KmsConfiguration toKmsConfiguration(KmsConfig config);

  @Mapping(target = "configUuid", source = "configUUID")
  @Mapping(target = "provider", expression = "java(source.getKeyProvider().name())")
  @Mapping(
      target = "inUse",
      expression =
          "java(com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.configInUse(source.getConfigUUID()))")
  @Mapping(target = "universeDetails", ignore = true)
  KmsConfigurationMetadata toKmsConfigurationMetadata(KmsConfig source);

  // workaround for collectionMappingStrategy = ADDER_PREFERRED in CentralConfig
  @AfterMapping
  default void fillKmsConfigurationUniverseDetails(
      KmsConfig source, @MappingTarget KmsConfigurationMetadata metadata) {
    metadata.setUniverseDetails(
        EncryptionAtRestUtil.getUniverses(source.getConfigUUID()).stream()
            .map(UniverseDetailSubsetMapper.INSTANCE::toV2)
            .collect(Collectors.toList()));
  }

  @Named("maskedCredentialsFromAuth")
  default Map<String, Object> maskedCredentialsFromAuth(ObjectNode auth) {
    if (auth == null) {
      return Collections.emptyMap();
    }
    return Json.mapper().convertValue(CommonUtils.maskConfig(auth), new TypeReference<>() {});
  }
}
