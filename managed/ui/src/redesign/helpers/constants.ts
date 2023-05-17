import { UniverseState } from '../../components/universes/helpers/universeHelpers';

export const YBTableRelationType = {
  SYSTEM_TABLE_RELATION: 'SYSTEM_TABLE_RELATION',
  USER_TABLE_RELATION: 'USER_TABLE_RELATION',
  INDEX_TABLE_RELATION: 'INDEX_TABLE_RELATION',
  MATVIEW_TABLE_RELATION: 'MATVIEW_TABLE_RELATION'
} as const;
export type YBTableRelationType = typeof YBTableRelationType[keyof typeof YBTableRelationType];

export const YBAHost = {
  GCP: 'gcp',
  AWS: 'aws',
  SELF_HOSTED: 'selfHosted'
} as const;

export const UnavailableUniverseStates = [UniverseState.PAUSED, UniverseState.PENDING] as const;

/**
 * Values are mapped to the sort order strings from
 * react-boostrap-table ('asc', 'desc').
 */
export const SortOrder = {
  ASCENDING: 'asc',
  DESCENDING: 'desc'
} as const;
export type SortOrder = typeof SortOrder[keyof typeof SortOrder];

export const RuntimeConfigKey = {
  PROVIDER_REDESIGN_FEATURE_FLAG: 'yb.ui.feature_flags.provider_redesign',
  XCLUSTER_TRANSACTIONAL_ATOMICITY_FEATURE_FLAG: 'yb.xcluster.transactional.enabled'
} as const;

/**
* Toast notification duration in milliseconds.
*/
export const ToastNotificationDuration = {
  SHORT: 2000,
  DEFAULT: 3000,
  LONG: 5000
}
