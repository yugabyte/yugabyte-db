/*
 * Created on Mon Jul 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { createStateContext } from 'react-use';
import { AuthGroupToRolesMapping } from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';

export default createStateContext<AuthGroupToRolesMapping | null>(null);

export const Pages = {
  CREATE_GROUP: 'CREATE_GROUP',
  LIST_GROUP: 'LIST_GROUP',
  EDIT_GROUP: 'EDIT_GROUP'
} as const;

export type Page = keyof typeof Pages;

export type GroupViewContext = {
  formProps: {
    currentPage: Page;
  };
  currentGroup: AuthGroupToRolesMapping | null;
};

export const initialGroupContextState: GroupViewContext = {
  formProps: {
    currentPage: Pages.LIST_GROUP
  },
  currentGroup: null
};

export const GroupViewContext = createContext<GroupViewContext>(initialGroupContextState);

export const groupMethods = (context: GroupViewContext) => ({
  setCurrentPage: (page: Page): GroupViewContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      currentPage: page
    }
  }),
  setCurrentGroup: (currentGroup: AuthGroupToRolesMapping | null): GroupViewContext => ({
    ...context,
    currentGroup
  })
});

export type GroupContextMethods = [GroupViewContext, ReturnType<typeof groupMethods>];
