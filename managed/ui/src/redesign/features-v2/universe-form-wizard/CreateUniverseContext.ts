/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { GeneralSettingsProps } from './steps/general-settings/dtos';

export enum CreateUniverseSteps {
  GENERAL_SETTINGS = 1,
  RESILIENCE_AND_REGIONS = 2,
  NODES_AND_AVAILABILITY = 3,
  HARDWARE = 4,
  DATABASE = 5,
  SECURITY = 6,
  ADVANCED = 7,
  REVIEW = 8
}

export type createUniverseFormProps = {
  activeStep: number;
  generalSettings?: GeneralSettingsProps;
};

export const initialCreateUniverseFormState: createUniverseFormProps = {
  activeStep: CreateUniverseSteps.GENERAL_SETTINGS
};

export const CreateUniverseContext = createContext<createUniverseFormProps>(
  initialCreateUniverseFormState
);

export const createUniverseFormMethods = (context: createUniverseFormProps) => ({
  moveToNextPage: () => ({
    ...context,
    activeStep: context.activeStep + 1
  }),
  moveToPreviousPage: () => ({
    ...context,
    activeStep: Math.max(context.activeStep - 1, 1)
  }),
  saveGeneralSettings: (data: GeneralSettingsProps) => ({
    ...context,
    generalSettings: data
  })
});

export type CreateUniverseContextMethods = [
  createUniverseFormProps,
  ReturnType<typeof createUniverseFormMethods>
];

// Navigate berween pages
export type StepsRef = {
  onNext: () => void;
  onPrev: () => void;
};
