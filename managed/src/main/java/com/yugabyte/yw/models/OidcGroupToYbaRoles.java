package com.yugabyte.yw.models;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbArray;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Entity
@Table(name = "oidc_group_to_yba_roles")
@Data
@EqualsAndHashCode(callSuper = false)
public class OidcGroupToYbaRoles extends Model {
  @Id
  @Column(name = "uuid", nullable = false, unique = true)
  public UUID uuid = UUID.randomUUID();

  @Column(name = "customer_uuid", nullable = false)
  public UUID customerUUID;

  @Column(name = "group_name", nullable = false, unique = true)
  public String groupName = null;

  @Column(name = "yba_roles", nullable = false)
  @DbArray
  public List<UUID> ybaRoles;

  public static OidcGroupToYbaRoles create(
      UUID customerUUID, String groupName, List<UUID> ybaRoles) {
    OidcGroupToYbaRoles entity = new OidcGroupToYbaRoles();
    entity.customerUUID = customerUUID;
    entity.groupName = groupName;
    entity.ybaRoles = ybaRoles;
    entity.save();
    return entity;
  }

  public static final Finder<UUID, OidcGroupToYbaRoles> find =
      new Finder<UUID, OidcGroupToYbaRoles>(OidcGroupToYbaRoles.class) {};
}
