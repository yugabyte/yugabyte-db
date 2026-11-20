import { describe, expect, it } from 'vitest';

import {
  PromExportType,
  SupportBundleComponentType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import {
  getAvailableCategories,
  getRecommendedComponents,
  PG_AUDIT_LOGS,
  SupportBundleSelectableComponent
} from './categories';
import { getDaysUntilExpiry } from './bundleDisplay';
import { LARGE_BUNDLE_THRESHOLD_BYTES, summarizeEstimate } from './estimate';
import { CollectionMode, getDefaultFormValues, SupportBundleFormValues } from './formState';
import { toCreateSpec } from './toCreateSpec';

const VM_CONTEXT = { isK8sUniverse: false, isPerfAdvisorRegistered: false };
const NOW = new Date('2026-07-26T14:32:00Z');

const customizeValues = (
  customComponents: SupportBundleSelectableComponent[]
): SupportBundleFormValues => ({
  ...getDefaultFormValues(getRecommendedComponents(VM_CONTEXT)),
  mode: CollectionMode.CUSTOMIZE,
  customComponents
});

const availableComponents = (context: typeof VM_CONTEXT) =>
  getAvailableCategories(context).flatMap((category) =>
    category.components.map((def) => def.component)
  );

describe('categories', () => {
  it('offers only the components the server collects for the universe type', () => {
    const vm = availableComponents(VM_CONTEXT);
    expect(vm).not.toContain(SupportBundleComponentType.K8sInfo);
    expect(vm).not.toContain(SupportBundleComponentType.PerfAdvisor);
    expect(vm).toContain(SupportBundleComponentType.SystemLogs);
    expect(vm).toContain(SupportBundleComponentType.NodeAgent);

    const k8sWithPa = availableComponents({ isK8sUniverse: true, isPerfAdvisorRegistered: true });
    expect(k8sWithPa).toContain(SupportBundleComponentType.K8sInfo);
    expect(k8sWithPa).toContain(SupportBundleComponentType.PerfAdvisor);
    // A pod has neither the host's /var/log nor a node agent.
    expect(k8sWithPa).not.toContain(SupportBundleComponentType.SystemLogs);
    expect(k8sWithPa).not.toContain(SupportBundleComponentType.NodeAgent);
  });
});

// Rules the backend depends on that aren't a field-for-field copy of the form.
describe('toCreateSpec', () => {
  it('collects PG audit logs through UniverseLogs with audit filtering turned off', () => {
    const spec = toCreateSpec(customizeValues([PG_AUDIT_LOGS]), VM_CONTEXT, NOW);

    expect(spec?.components).toEqual([SupportBundleComponentType.UniverseLogs]);
    expect(spec?.filter_pg_audit_logs).toBe(false);
  });

  it('sends custom queries and always downsamples a PromQL export', () => {
    const values = customizeValues([SupportBundleComponentType.PrometheusMetrics]);
    values.metrics = {
      ...values.metrics,
      promExportType: PromExportType.PROMQL,
      promDumpDownSample: false,
      promQueries: { cpu: 'up' }
    };

    const spec = toCreateSpec(values, VM_CONTEXT, NOW);

    expect(spec?.prom_dump_down_sample).toBe(true);
    expect(spec?.prom_queries).toEqual({ cpu: 'up' });
  });
});

describe('summarizeEstimate', () => {
  it('totals sizes across nodes and the YBA host, by component and by category', () => {
    const summary = summarizeEstimate({
      data: {
        YBA: { ApplicationLogs: 100 },
        n1: { UniverseLogs: 200, ConsensusMeta: 5 },
        n2: { UniverseLogs: 300 }
      }
    });

    expect(summary.totalBytes).toBe(605);
    expect(summary.componentBytes.UniverseLogs).toBe(500);
    expect(summary.categoryBytes).toMatchObject({
      platformLogs: 100,
      clusterLogs: 500,
      internalMetadata: 5
    });
    expect(summary.isLarge).toBe(false);
  });

  it('flags bundles over 20 GB', () => {
    const summary = summarizeEstimate({
      data: { n1: { UniverseLogs: LARGE_BUNDLE_THRESHOLD_BYTES + 1 } }
    });

    expect(summary.isLarge).toBe(true);
  });
});

describe('getDaysUntilExpiry', () => {
  it('rounds up to whole days and never goes negative', () => {
    expect(getDaysUntilExpiry('2026-08-04T00:00:00Z', NOW)).toBe(9);
    expect(getDaysUntilExpiry('2026-07-01T00:00:00Z', NOW)).toBe(0);
  });
});
