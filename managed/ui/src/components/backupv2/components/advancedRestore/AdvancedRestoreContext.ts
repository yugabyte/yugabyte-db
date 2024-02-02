/*
 * Created on Tue Jan 02 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { getNextPage, getPrevPage } from './AdvancedRestoreUtils';
import { Pages, RestoreContext } from '../restore/RestoreContext';

export const AdvancedRestorePages = Pages;

export type Page = typeof AdvancedRestorePages[number];

export type formWizardProps = {
  currentPage: Page;
  submitLabel: string; // label of the ybmodal submit
  disableSubmit: boolean; //disable the submit button
  isSubmitting: boolean;
};

export type AdvancedRestoreContext = {
  formProps: formWizardProps;
};

export const initialAdvancedRestoreContextState: AdvancedRestoreContext = {
  formProps: {
    currentPage: 'GENERAL_SETTINGS', // default page to show
    submitLabel: 'Restore',
    disableSubmit: false,
    isSubmitting: false
  } as formWizardProps
};

export const AdvancedRestoreFormContext = createContext<AdvancedRestoreContext>(
  initialAdvancedRestoreContextState
);

export const AdvancedRestoreMethods = (context: AdvancedRestoreContext) => {
  return {
    moveToNextPage: (baseRestoreContext: RestoreContext) =>
      getNextPage(context, baseRestoreContext),
    moveToPrevPage: (baseRestoreContext: RestoreContext) =>
      getPrevPage(context, baseRestoreContext),
    moveToPage: (page: Page): AdvancedRestoreContext => ({
      ...context,
      formProps: {
        ...context.formProps,
        currentPage: page
      }
    }),
    setSubmitLabel: (text: formWizardProps['submitLabel']): AdvancedRestoreContext => ({
      ...context,
      formProps: {
        ...context.formProps,
        submitLabel: text
      }
    }),

    setDisableSubmit: (flag: boolean): AdvancedRestoreContext => ({
      ...context,
      formProps: {
        ...context.formProps,
        disableSubmit: flag
      }
    }),
    setisSubmitting: (flag: boolean): AdvancedRestoreContext => ({
      ...context,
      formProps: {
        ...context.formProps,
        isSubmitting: flag
      }
    })
  };
};

export type AdvancedRestoreContextMethods = [
  AdvancedRestoreContext,
  ReturnType<typeof AdvancedRestoreMethods>,
  {
    hideModal: () => void;
  }
];

export type PageRef = {
  onNext: () => void;
  onPrev: () => void;
};
