import { createContext } from 'react';
import { UniverseRespResponse } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { RRDatabaseSettingsProps } from './steps/RRDatabaseSettings/dtos';
import { RRInstanceSettingsProps } from './steps';
import { RRRegionsAndAZSettings } from './steps/RRRegionsAndAZ/dtos';

export interface AddRRContextProps {
  activeStep: number;
  /** Route param `/universes/:uuid/add-read-replica` — used for API and navigation. */
  universeUuid: string;
  instanceSettings?: RRInstanceSettingsProps;
  databaseSettings?: RRDatabaseSettingsProps;
  universeData?: UniverseRespResponse;
  regionsAndAZ?: RRRegionsAndAZSettings;
  /** Deep clone of initial regions/AZ (from universe or default); used for per-region Undo on existing rows. */
  regionsAndAZBaseline?: RRRegionsAndAZSettings;
  /** True when initial placement was loaded from the existing read-replica cluster (enables per-AZ Undo). */
  readReplicaPlacementFromUniverse?: boolean;
}

export enum AddReadReplicaSteps {
  REGIONS_AND_AZ = 1,
  INSTANCE,
  DATABASE,
  REVIEW
}

export const initialAddRRFormState: AddRRContextProps = {
  activeStep: AddReadReplicaSteps.REGIONS_AND_AZ,
  universeUuid: '',
  databaseSettings: { gFlags: [], customizeRRFlags: false }
};

export const AddRRContext = createContext<AddRRContextProps>(initialAddRRFormState);

export const addRRFormMethods = (context: AddRRContextProps) => ({
  initializeDefaultValues: (data: Partial<AddRRContextProps>) => {
    const preserveHardware =
      context.activeStep >= AddReadReplicaSteps.INSTANCE && context.instanceSettings;
    const preserveDatabase =
      context.activeStep >= AddReadReplicaSteps.DATABASE && context.databaseSettings;

    return {
      ...context,
      ...data,
      universeUuid: data.universeUuid ?? context.universeUuid,
      // Preserve wizard progress and in-flight placement when the universe query refetches.
      regionsAndAZ: context.regionsAndAZ ?? data.regionsAndAZ,
      regionsAndAZBaseline: context.regionsAndAZBaseline ?? data.regionsAndAZBaseline,
      readReplicaPlacementFromUniverse:
        context.readReplicaPlacementFromUniverse ?? data.readReplicaPlacementFromUniverse,
      activeStep:
        context.activeStep > AddReadReplicaSteps.REGIONS_AND_AZ
          ? context.activeStep
          : (data.activeStep ?? context.activeStep),
      // `...data` would reset hardware/DB on every refetch; keep user edits after those steps.
      instanceSettings: preserveHardware
        ? context.instanceSettings
        : (data.instanceSettings ?? context.instanceSettings),
      databaseSettings: preserveDatabase
        ? context.databaseSettings
        : (data.databaseSettings ?? context.databaseSettings)
    };
  },
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
  }),
  saveRegionsAndAZSettings: (data: RRRegionsAndAZSettings) => ({
    ...context,
    regionsAndAZ: data
  }),
  /** Keeps baseline rows aligned with form rows when removing a region (indices stay stable). */
  spliceRegionsAndAZBaseline: (index: number) => {
    const baseline = context.regionsAndAZBaseline;
    if (!baseline?.regions?.length || index < 0 || index >= baseline.regions.length) {
      return context;
    }
    return {
      ...context,
      regionsAndAZBaseline: {
        regions: baseline.regions.filter((_, i) => i !== index)
      }
    };
  }
});

export type AddRRContextMethods = [AddRRContextProps, ReturnType<typeof addRRFormMethods>];

export type StepsRef = {
  onNext: () => Promise<void>;
  onPrev: () => void;
};
