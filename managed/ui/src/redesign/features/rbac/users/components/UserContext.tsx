/*
 * Created on Tue Aug 08 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { browserHistory } from 'react-router';
import { RbacUserWithResources } from '../interface/Users';
import { UniverseResource } from '../../policy/IPolicy';
import { UsersTab } from '../../common/rbac_constants';

export const UserPages = {
  CREATE_USER: 'CREATE_USER',
  LIST_USER: 'LIST_USER',
  EDIT_USER: 'EDIT_USER'
} as const;

export type UserPage = keyof typeof UserPages;

export type UserContext<T> = {
  formProps: {
    currentPage: UserPage;
  };
  currentUser: RbacUserWithResources | null;
};

export const defaultUserContext: UserContext<UniverseResource> = {
  currentUser: null,
  formProps: {
    currentPage: 'LIST_USER'
  }
};

export const UserViewContext = createContext<UserContext<UniverseResource>>(defaultUserContext);

export const userMethods = (context: UserContext<UniverseResource>) => ({
  setCurrentPage: (page: UserPage): UserContext<UniverseResource> => {
    browserHistory.push(UsersTab);
    return {
      ...context,
      formProps: {
        ...context.formProps,
        currentPage: page
      }
    };
  },
  setCurrentUser: (currentUser: RbacUserWithResources | null): UserContext<UniverseResource> => ({
    ...context,
    currentUser
  })
});

export type UserContextMethods = [UserContext<UniverseResource>, ReturnType<typeof userMethods>];
