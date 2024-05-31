/*
 * Created on Wed Aug 23 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { IRestore } from '../common/IRestore';

export type RestoreDetailsContext = {
  selectedRestore: IRestore | null;
};

export const initialRestoreDetailsContextState: RestoreDetailsContext = {
  selectedRestore: null
};

export const RestoreDetailsContext = createContext<RestoreDetailsContext>(
  initialRestoreDetailsContextState
);

export const restoreDetailsMethods = (context: RestoreDetailsContext) => ({
  setSelectedRestore: (selectedRestore: IRestore | null): RestoreDetailsContext => ({
    ...context,
    selectedRestore
  })
});

export type RestoreContextMethods = [
  RestoreDetailsContext,
  ReturnType<typeof restoreDetailsMethods>
];
