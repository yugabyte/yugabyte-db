import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { createContext } from 'react';
import { Region } from '@app/redesign/helpers/dtos';

export enum EditUniverseTabs {
  GENERAL = 'general',
  PLACEMENT = 'placement',
  HARDWARE = 'hardware',
  SECURITY = 'security',
  DATABASE = 'database',
  ADVANCED = 'advanced'
}

export type EditUniverseContextProps = {
  activeTab: EditUniverseTabs;
  universeData: Universe | null;
  providerRegions: Region[];
};

export const InitialEditUniverseContextState: EditUniverseContextProps = {
  activeTab: EditUniverseTabs.GENERAL,
  universeData: null,
  providerRegions: []
};

export const EditUniverseContext = createContext<EditUniverseContextProps>(
  InitialEditUniverseContextState
);
