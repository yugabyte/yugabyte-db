/*
 * Created on Mon Jul 29 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import * as yup from 'yup';
import { TFunction } from 'i18next';
import { isEqual, uniq, uniqWith } from 'lodash';
import { AuthGroupToRolesMapping } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { RbacUserWithResources } from '../../users/interface/Users';
import { RoleType } from '../../roles';

export const getGroupValidationSchema = (t: TFunction) => {
  const validationSchema = yup.object<Partial<AuthGroupToRolesMapping>>({
    group_identifier: yup.string().required(t('groupNameRequired')),
    type: yup.string().required() as any
  }) as any;

  return validationSchema;
};

export const getRoleResourceValidationSchema = (t: TFunction) => {
  return yup.object({
    roleResourceDefinitions: yup
      .array()
      .test({
        name: 'should not have empty roles',
        message: t('roleRequired', { keyPrefix: 'rbac.users.validationMessages' }),
        test: function (val: RbacUserWithResources['roleResourceDefinitions']) {
          return val?.every((v) => v.role !== null);
        } as any
      })
      .test({
        name: 'should not have duplicate built-in roles',
        message: t('duplicateBuiltinRoles', { keyPrefix: 'rbac.users.validationMessages' }),
        test: function (val: RbacUserWithResources['roleResourceDefinitions']) {
          const builtInRoles = val
            ?.filter((v) => v?.role?.roleType === RoleType.SYSTEM)
            .map((r) => r.role?.roleUUID);
          return uniq(builtInRoles).length === builtInRoles?.length;
        } as any
      })
      .test({
        name: 'should not have duplicate custom roles',
        message: t('duplicateCustomRoles', { keyPrefix: 'rbac.users.validationMessages' }),
        test: function (val: RbacUserWithResources['roleResourceDefinitions']) {
          const customRoles = val?.filter((v) => v.role?.roleType === RoleType.CUSTOM);
          return (
            uniqWith(customRoles, (a, b) => {
              return isEqual(a.role, b.role);
            }).length === customRoles?.length
          );
        } as any
      })
  }) as any;
};
