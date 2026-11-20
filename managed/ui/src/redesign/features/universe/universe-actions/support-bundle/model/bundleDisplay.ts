import moment from 'moment';
import { StatusType } from '@yugabyte-ui-library/core';

import {
  SupportBundle,
  SupportBundleComponentType,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { isSensitiveComponent } from './categories';

const ONE_DAY_MS = 24 * 60 * 60 * 1000;

/**
 * `creation_date` stays null until collection succeeds, so a running bundle is named after its
 * collection end date, which defaults to the submit time.
 */
export const getSupportBundleName = (bundle: SupportBundle): string => {
  const timestamp = bundle.info?.creation_date ?? bundle.spec?.end_date;
  return timestamp
    ? `support-bundle-${moment(timestamp).format('YYYY-MM-DD-HHmm')}`
    : `support-bundle-${(bundle.info?.uuid ?? '').slice(0, 8)}`;
};

export const getDaysUntilExpiry = (expirationDate: string | null | undefined, now: Date) => {
  if (!expirationDate) {
    return null;
  }
  return Math.max(0, Math.ceil((new Date(expirationDate).getTime() - now.getTime()) / ONE_DAY_MS));
};

export interface SupportBundleStatusDisplay {
  statusType: StatusType;
  /** Translation key under `supportBundleV2.status`. */
  i18nKey: string;
}

export const getSupportBundleStatusDisplay = (
  status: SupportBundleStatus | undefined
): SupportBundleStatusDisplay => {
  switch (status) {
    case SupportBundleStatus.Success:
      return { statusType: StatusType.SUCCESS, i18nKey: 'ready' };
    case SupportBundleStatus.Failed:
      return { statusType: StatusType.ERROR, i18nKey: 'failed' };
    case SupportBundleStatus.Aborted:
      return { statusType: StatusType.INACTIVE, i18nKey: 'aborted' };
    case SupportBundleStatus.Running:
    default:
      return { statusType: StatusType.IN_PROGRESS, i18nKey: 'creating' };
  }
};

export const isSupportBundleRunning = (bundle: SupportBundle) =>
  bundle.info?.status === SupportBundleStatus.Running;

/**
 * Sensitive components the bundle was collected with. PG audit logs can't be reported: the
 * persisted SupportBundleSpec doesn't keep filter_pg_audit_logs.
 */
export const getSensitiveComponents = (bundle: SupportBundle): SupportBundleComponentType[] =>
  (bundle.spec?.components ?? []).filter(isSensitiveComponent);
