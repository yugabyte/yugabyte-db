// Copyright (c) YugaByte, Inc.

package api.v2.mappers;

import api.v2.models.User;
import api.v2.models.UserInfo;
import api.v2.models.UserSpec;
import com.yugabyte.yw.models.Users;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;

@Mapper(config = CentralConfig.class)
public interface UserMapper {

  @Mapping(target = "spec", source = "v1User")
  @Mapping(target = "info", source = "v1User")
  User toV2User(Users v1User);

  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "isPrimary", source = "primary")
  UserInfo toV2UserInfo(Users v1User);

  @ValueMappings({
    @ValueMapping(target = "CONNECTONLY", source = "ConnectOnly"),
    @ValueMapping(target = "READONLY", source = "ReadOnly"),
    @ValueMapping(target = "BACKUPADMIN", source = "BackupAdmin"),
    @ValueMapping(target = "ADMIN", source = "Admin"),
    @ValueMapping(target = "SUPERADMIN", source = "SuperAdmin"),
  })
  UserSpec.RoleEnum toV2Role(Users.Role v1Role);

  @ValueMappings({
    @ValueMapping(target = "LOCAL", source = "local"),
    @ValueMapping(target = "LDAP", source = "ldap"),
    @ValueMapping(target = "OIDC", source = "oidc"),
  })
  UserInfo.UserTypeEnum toV2UserType(Users.UserType v1UserType);

  default OffsetDateTime toOffsetDateTime(Date date) {
    if (date == null) {
      return null;
    }
    return date.toInstant().atOffset(ZoneOffset.UTC);
  }
}
