/*
 * Created on Thu Jan 04 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { currentPageIsGeneralSettings } from '../restore/RestoreUtils';
import { RestoreContext } from '../restore/RestoreContext';
import { AdvancedRestoreContext } from './AdvancedRestoreContext';

export const getNextPage = (
  advancedRestoreContext: AdvancedRestoreContext,
  baseRestoreContext: RestoreContext
): AdvancedRestoreContext => {
  const {
    formProps: { currentPage }
  } = advancedRestoreContext;
  const {
    formData: { generalSettings }
  } = baseRestoreContext;

  switch (currentPage) {
    case 'GENERAL_SETTINGS': {
      const context = currentPageIsGeneralSettings(baseRestoreContext);

      return {
        ...advancedRestoreContext,
        formProps: {
          ...advancedRestoreContext.formProps,
          currentPage: context.formProps.currentPage
        }
      };
    }
    case 'RENAME_KEYSPACES':
      return {
        ...advancedRestoreContext,
        formProps: {
          ...advancedRestoreContext.formProps,
          currentPage:
            generalSettings?.tableSelectionType === 'SUBSET_OF_TABLES'
              ? 'SELECT_TABLES'
              : 'RESTORE_FINAL'
        }
      };
    case 'SELECT_TABLES':
      return {
        ...advancedRestoreContext,
        formProps: {
          ...advancedRestoreContext.formProps,
          currentPage: 'RESTORE_FINAL'
        }
      };
    default:
      return advancedRestoreContext;
  }
};

export const getPrevPage = (
  advancedRestoreContext: AdvancedRestoreContext,
  baseRestoreContext: RestoreContext
): AdvancedRestoreContext => {
  const {
    formData: { generalSettings }
  } = baseRestoreContext;
  const {
    formProps: { currentPage }
  } = advancedRestoreContext;
  switch (currentPage) {
    case 'RENAME_KEYSPACES':
      return {
        ...advancedRestoreContext,
        formProps: {
          ...advancedRestoreContext.formProps,
          currentPage: 'GENERAL_SETTINGS'
        }
      };
    case 'SELECT_TABLES':
      return {
        ...advancedRestoreContext,
        formProps: {
          ...advancedRestoreContext.formProps,
          currentPage: generalSettings?.renameKeyspace ? 'RENAME_KEYSPACES' : 'GENERAL_SETTINGS'
        }
      };
    default:
      return advancedRestoreContext;
  }
};
