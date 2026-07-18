import { createContext } from 'react';
import { UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { RRDatabaseSettingsProps } from './steps/RRDatabaseSettings/dtos';
import { RRInstanceSettingsProps } from './steps';

export interface AddRRContextProps {
  activeStep: number;
  instanceSettings?: RRInstanceSettingsProps;
  databaseSettings?: RRDatabaseSettingsProps;
  universeData?: UniverseRespResponse;
}

export enum AddReadReplicaSteps {
  REGIONS_AND_AZ = 1,
  INSTANCE,
  DATABASE,
  REVIEW
}

export const initialAddRRFormState: AddRRContextProps = {
  activeStep: AddReadReplicaSteps.INSTANCE,
  databaseSettings: { gFlags: [], customizeRRFlags: false }
};

export const AddRRContext = createContext<AddRRContextProps>(initialAddRRFormState);

export const addRRFormMethods = (context: AddRRContextProps) => ({
  initializeDefaultValues: (data: AddRRContextProps) => ({
    ...context,
    ...data
  }),
  setActiveStep: (step: number) => ({
    ...context,
    activeStep: step
  }),
  moveToNextPage: () => ({
    ...context,
    activeStep: context.activeStep + 1
  }),
  moveToPreviousPage: () => ({
    ...context,
    activeStep: Math.max(context.activeStep - 1, 1)
  }),
  setUniverseData: (universeData: UniverseRespResponse) => ({
    ...context,
    universeData
  }),
  saveInstanceSettings: (data: RRInstanceSettingsProps) => ({
    ...context,
    instanceSettings: data
  }),
  saveDatabaseSettings: (data: RRDatabaseSettingsProps) => ({
    ...context,
    databaseSettings: data
  })
});

export type AddRRContextMethods = [AddRRContextProps, ReturnType<typeof addRRFormMethods>];

export type StepsRef = {
  onNext: () => Promise<void>;
  onPrev: () => void;
};
