import moment from 'moment';
import { StatusType } from '@yugabyte-ui-library/core';

import {
  SupportBundle,
  SupportBundleComponentType,
  SupportBundleStatus
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import {
  isSensitiveComponent,
  PG_AUDIT_LOGS,
  SupportBundleSelectableComponent
} from './categories';

const ONE_DAY_MS = 24 * 60 * 60 * 1000;

/** Named after its creation date, which the server sets when the bundle is requested. */
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

const PG_AUDIT_FILTER_FLAG = '--filter_pgaudit_logs';

/**
 * Sensitive components the bundle was collected with.
 *
 * The server's manifest merge rewrites `components` to group names (FilesComponent, ...) and moves
 * each file component into `files_component_specs`, so both are checked, as
 * SupportBundleHandlerV2.requestsCoreFiles does. PG audit logs ride on UniverseLogs: they are
 * included when its params turn audit filtering off.
 */
export const getSensitiveComponents = (
  bundle: SupportBundle
): SupportBundleSelectableComponent[] => {
  const fileSpecs = bundle.spec?.files_component_specs ?? [];
  const collected = new Set<string>([
    ...(bundle.spec?.components ?? []),
    ...fileSpecs.map((spec) => spec.component_name ?? '')
  ]);
  const sensitive: SupportBundleSelectableComponent[] = [
    SupportBundleComponentType.CoreFiles
  ].filter((component) => collected.has(component));
  const includesPgAuditLogs = fileSpecs.some((spec) => {
    if (spec.component_name !== SupportBundleComponentType.UniverseLogs) {
      return false;
    }
    const params = spec.params ?? [];
    const flagIndex = params.indexOf(PG_AUDIT_FILTER_FLAG);
    return flagIndex >= 0 && params[flagIndex + 1] === 'false';
  });
  if (includesPgAuditLogs) {
    sensitive.push(PG_AUDIT_LOGS);
  }
  return sensitive.filter(isSensitiveComponent);
};
