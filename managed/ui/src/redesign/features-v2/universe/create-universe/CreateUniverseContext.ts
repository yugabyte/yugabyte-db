/*
 * Created on Tue Mar 25 2025
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { createContext } from 'react';
import { GeneralSettingsProps } from './steps/general-settings/dtos';
import {
  FaultToleranceType,
  ResilienceAndRegionsProps,
  ResilienceFormMode,
  ResilienceType
} from './steps/resilence-regions/dtos';
import { NodeAvailabilityProps } from './steps/nodes-availability/dtos';
import { InstanceSettingProps } from './steps/hardware-settings/dtos';
import { DatabaseSettingsProps } from './steps/database-settings/dtos';
import { SecuritySettingsProps } from './steps/security-settings/dtos';
import { OtherAdvancedProps, ProxyAdvancedProps } from './steps/advanced-settings/dtos';
import {
  FAULT_TOLERANCE_TYPE,
  NODE_COUNT,
  REGIONS_FIELD,
  REPLICATION_FACTOR,
  RESILIENCE_FORM_MODE,
  RESILIENCE_TYPE
} from './fields/FieldNames';
import { ArchitectureType } from '@app/components/configRedesign/providerRedesign/constants';

export enum CreateUniverseSteps {
  GENERAL_SETTINGS = 1,
  RESILIENCE_AND_REGIONS = 2,
  NODES_AVAILABILITY = 3,
  INSTANCE = 4,
  DATABASE = 5,
  SECURITY = 6,
  ADVANCED_PROXY = 7,
  ADVANCED_OTHER = 8,
  REVIEW = 9
}

export type createUniverseFormProps = {
  activeStep: number;
  generalSettings?: GeneralSettingsProps;
  resilienceAndRegionsSettings?: ResilienceAndRegionsProps;
  nodesAvailabilitySettings?: NodeAvailabilityProps;
  instanceSettings?: InstanceSettingProps;
  databaseSettings?: DatabaseSettingsProps;
  securitySettings?: SecuritySettingsProps;
  proxySettings?: ProxyAdvancedProps;
  otherAdvancedSettings?: OtherAdvancedProps;
  resilienceType?: ResilienceType;
};

export const initialCreateUniverseFormState: createUniverseFormProps = {
  activeStep: CreateUniverseSteps.GENERAL_SETTINGS,
  resilienceAndRegionsSettings: {
    [RESILIENCE_TYPE]: ResilienceType.REGULAR,
    [RESILIENCE_FORM_MODE]: ResilienceFormMode.GUIDED,
    [REGIONS_FIELD]: [],
    [REPLICATION_FACTOR]: 3,
    [FAULT_TOLERANCE_TYPE]: FaultToleranceType.AZ_LEVEL,
    [NODE_COUNT]: 1
  },
  nodesAvailabilitySettings: {
    availabilityZones: {},
    nodeCountPerAz: 1,
    useDedicatedNodes: false
  },
  databaseSettings: {
    ysql: {
      enable: true,
      enable_auth: false,
      password: ''
    },
    ycql: {
      enable: true,
      enable_auth: false,
      password: ''
    },
    gFlags: [],
    enableConnectionPooling: false,
    enablePGCompatibitilty: false
  },
  instanceSettings: {
    arch: ArchitectureType.X86_64,
    imageBundleUUID: '',
    useSpotInstance: true,
    instanceType: null,
    masterInstanceType: null,
    deviceInfo: null,
    masterDeviceInfo: null,
    tserverK8SNodeResourceSpec: null,
    masterK8SNodeResourceSpec: null,
    keepMasterTserverSame: true,
    enableEbsVolumeEncryption: false,
    ebsKmsConfigUUID: null
  },
  securitySettings: {
    enableClientToNodeEncryption: false,
    enableNodeToNodeEncryption: false
  },
  resilienceType: ResilienceType.REGULAR,
  proxySettings: {
    enableProxyServer: false,
    secureWebProxy: false,
    secureWebProxyServer: '',
    secureWebProxyPort: undefined,
    webProxy: false,
    byPassProxyList: false,
    byPassProxyListValues: []
  }
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
  setActiveStep: (step: CreateUniverseSteps) => ({
    ...context,
    activeStep: step
  }),
  saveGeneralSettings: (data: GeneralSettingsProps) => ({
    ...context,
    generalSettings: data
  }),
  saveResilienceAndRegionsSettings: (data: ResilienceAndRegionsProps) => ({
    ...context,
    resilienceAndRegionsSettings: data
  }),
  saveNodesAvailabilitySettings: (data: NodeAvailabilityProps) => ({
    ...context,
    nodesAvailabilitySettings: data
  }),
  saveInstanceSettings: (data: InstanceSettingProps) => ({
    ...context,
    instanceSettings: data
  }),
  saveDatabaseSettings: (data: DatabaseSettingsProps) => ({
    ...context,
    databaseSettings: data
  }),
  saveSecuritySettings: (data: SecuritySettingsProps) => ({
    ...context,
    securitySettings: data
  }),
  saveProxySettings: (data: ProxyAdvancedProps) => ({
    ...context,
    proxySettings: data
  }),
  saveOtherAdvancedSettings: (data: OtherAdvancedProps) => ({
    ...context,
    otherAdvancedSettings: data
  }),
  setResilienceType: (resilienceType: ResilienceType) => ({
    ...context,
    resilienceType
  })
});

export type CreateUniverseContextMethods = [
  createUniverseFormProps,
  ReturnType<typeof createUniverseFormMethods>
];

// Navigate between pages
export type StepsRef = {
  onNext: () => Promise<void>;
  onPrev: () => void;
};
