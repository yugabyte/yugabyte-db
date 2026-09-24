import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { createContext } from 'react';
import { Region } from '@app/redesign/helpers/dtos';

export enum EditUniverseTabs {
  GENERAL = 'general',
  PLACEMENT = 'placement',
  HARDWARE = 'hardware',
  SECURITY = 'security',
  DATABASE = 'database',
  ADVANCED = 'advanced',
  LOGS = 'logs',
  TELEMETRY_EXPORT = 'telemetry-export'
}

export type EditUniverseContextProps = {
  activeTab: EditUniverseTabs;
  universeData: Universe | null;
  providerRegions: Region[];
  /** True when K8s operator owns this universe and API mutations are blocked. */
  isK8OperatorEditBlocked?: boolean;
};

export const InitialEditUniverseContextState: EditUniverseContextProps = {
  activeTab: EditUniverseTabs.GENERAL,
  universeData: null,
  providerRegions: [],
  isK8OperatorEditBlocked: false
};

export const EditUniverseContext = createContext<EditUniverseContextProps>(
  InitialEditUniverseContextState
);
