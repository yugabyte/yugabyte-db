/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import com.yugabyte.yw.models.Users.Role;
import io.ebean.Finder;
import io.ebean.Model;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import lombok.EqualsAndHashCode;

@Entity
@EqualsAndHashCode(callSuper = false)
public class LdapDnToYbaRole extends Model {

  @Id
  @Column(nullable = false, unique = true)
  public UUID uuid = UUID.randomUUID();

  @Column(nullable = false)
  public UUID customerUUID;

  @Column(nullable = false, unique = true)
  public String distinguishedName = null;

  @Column(nullable = false)
  public Role ybaRole;

  public static LdapDnToYbaRole create(UUID customerUUID, String distinguishedName, Role ybaRole) {
    LdapDnToYbaRole LdapDnToYbaRole = new LdapDnToYbaRole();
    LdapDnToYbaRole.customerUUID = customerUUID;
    LdapDnToYbaRole.distinguishedName = distinguishedName;
    LdapDnToYbaRole.ybaRole = ybaRole;
    LdapDnToYbaRole.save();
    return LdapDnToYbaRole;
  }

  public static final Finder<UUID, LdapDnToYbaRole> find =
      new Finder<UUID, LdapDnToYbaRole>(LdapDnToYbaRole.class) {};
}
