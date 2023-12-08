/*
 * Created on Tue Jul 18 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { createStateContext } from 'react-use';
import { IRole } from './IRoles';

// export const roleMethods
export default createStateContext<IRole | null>(null);

export const Pages = ['CREATE_ROLE', 'LIST_ROLE', 'EDIT_ROLE'] as const;

export type Page = typeof Pages[number];

export type RoleViewContext = {
  formProps: {
    currentPage: Page;
  };
  currentRole: IRole | null;
};

export const initialRoleContextState: RoleViewContext = {
  formProps: {
    currentPage: 'LIST_ROLE'
  },
  currentRole: null
};

export const RoleViewContext = createContext<RoleViewContext>(initialRoleContextState);

export const roleMethods = (context: RoleViewContext) => ({
  setCurrentPage: (page: Page): RoleViewContext => ({
    ...context,
    formProps: {
      ...context.formProps,
      currentPage: page
    }
  }),
  setCurrentRole: (currentRole: IRole | null): RoleViewContext => ({
    ...context,
    currentRole
  })
});

export type RoleContextMethods = [RoleViewContext, ReturnType<typeof roleMethods>];
