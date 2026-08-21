import { createContext, useContext, type ReactNode } from 'react';

import type { ClusterSpec, Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import type { ReleaseOption } from './types';

export interface DbUpgradeModalContextValue {
  currentUniverseUuid: string;
  universeDetails: Universe;
  currentDbVersion: string;
  clusters: ClusterSpec[];
  maxNodesPerBatchMaximum: number;
  targetReleaseOptions: ReleaseOption[];
  closeModal: () => void;
}

const DbUpgradeModalContext = createContext<DbUpgradeModalContextValue | undefined>(undefined);

interface DbUpgradeModalContextProviderProps {
  value: DbUpgradeModalContextValue;
  children: ReactNode;
}

export const DbUpgradeModalContextProvider = ({
  value,
  children
}: DbUpgradeModalContextProviderProps) => (
  <DbUpgradeModalContext.Provider value={value}>{children}</DbUpgradeModalContext.Provider>
);

export const useDbUpgradeModalContext = (): DbUpgradeModalContextValue => {
  const context = useContext(DbUpgradeModalContext);
  if (!context) {
    throw new Error('useDbUpgradeModalContext must be used within a DbUpgradeModalContextProvider');
  }
  return context;
};
