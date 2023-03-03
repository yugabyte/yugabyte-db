import { UniverseState } from '../../components/universes/helpers/universeHelpers';

export const YBTableRelationType = {
  SYSTEM_TABLE_RELATION: 'SYSTEM_TABLE_RELATION',
  USER_TABLE_RELATION: 'USER_TABLE_RELATION',
  INDEX_TABLE_RELATION: 'INDEX_TABLE_RELATION',
  MATVIEW_TABLE_RELATION: 'MATVIEW_TABLE_RELATION'
} as const;
// eslint-disable-next-line no-redeclare
export type YBTableRelationType = typeof YBTableRelationType[keyof typeof YBTableRelationType];

export const YBAHost = {
  GCP: 'gcp',
  AWS: 'aws',
  SELF_HOSTED: 'selfHosted'
};

export const UnavailableUniverseStates = [UniverseState.PAUSED, UniverseState.PENDING] as const;
