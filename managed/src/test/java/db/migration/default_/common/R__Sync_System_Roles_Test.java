/*
 * Copyright 2023 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import db.migration.default_.postgres.V383__Enable_Rbac;
import java.util.List;
import org.junit.Before;
import org.junit.Test;

public class R__Sync_System_Roles_Test extends FakeDBApplication {

  KmsConfig fakeKmsConfig;
  Customer testCustomer1;
  Customer testCustomer2;

  @Before
  public void setup() {
    testCustomer1 = ModelFactory.testCustomer();
    testCustomer2 = ModelFactory.testCustomer();
  }

  @Test
  public void checkMigrationSystemRolesCreated() {
    // Verify that no system roles are present before the migration for each customer.
    List<Role> rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    List<Role> rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);

    // Run the migration.
    R__Sync_System_Roles.syncSystemRoles();

    // Verify that 5 system roles are created after the migration for each customer.
    rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer1.size());
    rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer2.size());

    // Run migration again, but this time it should not add any new roles for both customers, since
    // they are already present.
    R__Sync_System_Roles.syncSystemRoles();

    // Verify that 5 system roles are present after the migration.
    rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer1.size());
    rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer2.size());
  }

  @Test
  public void checkMigrationOnlyMissingSystemRolesCreated() {
    List<Role> rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    List<Role> rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);

    // Run the migration.
    R__Sync_System_Roles.syncSystemRoles();

    // Verify that 5 system roles are created after the migration for the customers.
    rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer1.size());
    rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer2.size());

    // Delete some roles for each customer to unsync the DB from the migration.
    Role.get(testCustomer1.getUuid(), "ConnectOnly").delete();
    Role.get(testCustomer2.getUuid(), "ReadOnly").delete();
    Role.get(testCustomer2.getUuid(), "BackupAdmin").delete();

    // Verify that the roles are missing for customer 1.
    rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    assertEquals(4, rolesInDbForCustomer1.size());
    assertNull(Role.get(testCustomer1.getUuid(), "ConnectOnly"));

    // Verify that the roles are missing for customer 2.
    rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);
    assertEquals(3, rolesInDbForCustomer2.size());
    assertNull(Role.get(testCustomer2.getUuid(), "ReadOnly"));
    assertNull(Role.get(testCustomer2.getUuid(), "BackupAdmin"));

    // Run migration again, but this time it should add only the missing roles for the customers.
    R__Sync_System_Roles.syncSystemRoles();

    // Verify that 5 system roles are present after the migration for each customer.
    rolesInDbForCustomer1 = Role.getAll(testCustomer1.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer1.size());
    rolesInDbForCustomer2 = Role.getAll(testCustomer2.getUuid(), RoleType.System);
    assertEquals(5, rolesInDbForCustomer2.size());
  }

  @Test
  public void test383() {
    // This test is to ensure that the migration V383__Enable_Rbac is run successfully.
    // The migration should not throw any exceptions and should complete without issues.
    // If this test fails, most probably we need to add a newly added Action in
    // com.yugabyte.yw.models.migrations.V383.PermissionInfo.Action

    V383__Enable_Rbac migration = new V383__Enable_Rbac();
    migration.addDefaultRoleBindings();

    com.yugabyte.yw.models.migrations.V383.Role newRbacRole =
        com.yugabyte.yw.models.migrations.V383.Role.get(testCustomer1.getUuid(), "Admin");
  }
}
