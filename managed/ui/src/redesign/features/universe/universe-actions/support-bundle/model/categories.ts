import { SupportBundleComponentType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/**
 * Not a backend component type. PG audit logs are collected as part of UniverseLogs, and
 * selecting this sends `filter_pg_audit_logs: false`.
 */
export const PG_AUDIT_LOGS = 'PgAuditLogs' as const;

export type SupportBundleSelectableComponent = SupportBundleComponentType | typeof PG_AUDIT_LOGS;

export const SupportBundleCategoryId = {
  PLATFORM_LOGS: 'platformLogs',
  CLUSTER_LOGS: 'clusterLogs',
  NODE_DIAGNOSTICS: 'nodeDiagnostics',
  INTERNAL_METADATA: 'internalMetadata',
  PERFORMANCE_METRICS: 'performanceMetrics',
  SECURITY_AUDIT: 'securityAudit'
} as const;
export type SupportBundleCategoryId =
  (typeof SupportBundleCategoryId)[keyof typeof SupportBundleCategoryId];

export interface SupportBundleComponentDef {
  component: SupportBundleSelectableComponent;
  /** Translation key under `supportBundleV2.component`. */
  i18nKey: string;
  sensitive?: boolean;
  k8sOnly?: boolean;
  /** Needs the host's /var/log or a node agent; the server drops it on Kubernetes universes. */
  vmOnly?: boolean;
  requiresPerfAdvisor?: boolean;
}

export interface SupportBundleCategoryDef {
  id: SupportBundleCategoryId;
  /** Part of the recommended bundle by default; otherwise listed as optional. */
  recommended: boolean;
  components: SupportBundleComponentDef[];
}

export const SUPPORT_BUNDLE_CATEGORIES: SupportBundleCategoryDef[] = [
  {
    id: SupportBundleCategoryId.PLATFORM_LOGS,
    recommended: true,
    components: [
      { component: SupportBundleComponentType.ApplicationLogs, i18nKey: 'applicationLogs' },
      { component: SupportBundleComponentType.YbaMetadata, i18nKey: 'ybaMetadata' }
    ]
  },
  {
    id: SupportBundleCategoryId.CLUSTER_LOGS,
    recommended: true,
    components: [
      { component: SupportBundleComponentType.UniverseLogs, i18nKey: 'universeLogs' },
      { component: SupportBundleComponentType.TabletReport, i18nKey: 'tabletReport' },
      { component: SupportBundleComponentType.GFlags, i18nKey: 'gFlags' },
      { component: SupportBundleComponentType.Instance, i18nKey: 'instance' },
      { component: SupportBundleComponentType.YbcLogs, i18nKey: 'ybcLogs' },
      { component: SupportBundleComponentType.ClusterConfig, i18nKey: 'clusterConfig' },
      { component: SupportBundleComponentType.K8sInfo, i18nKey: 'k8sInfo', k8sOnly: true }
    ]
  },
  {
    id: SupportBundleCategoryId.NODE_DIAGNOSTICS,
    recommended: true,
    components: [
      { component: SupportBundleComponentType.SystemLogs, i18nKey: 'systemLogs', vmOnly: true },
      { component: SupportBundleComponentType.NodeAgent, i18nKey: 'nodeAgent', vmOnly: true },
      { component: SupportBundleComponentType.OutputFiles, i18nKey: 'outputFiles' },
      { component: SupportBundleComponentType.ErrorFiles, i18nKey: 'errorFiles' },
      // NodeHealthLogs is left out until the v2 manifest configures it; the v2 API drops it today.
      { component: SupportBundleComponentType.CoreFiles, i18nKey: 'coreFiles', sensitive: true }
    ]
  },
  {
    id: SupportBundleCategoryId.INTERNAL_METADATA,
    recommended: true,
    components: [
      { component: SupportBundleComponentType.ConsensusMeta, i18nKey: 'consensusMeta' },
      { component: SupportBundleComponentType.TabletMeta, i18nKey: 'tabletMeta' }
    ]
  },
  {
    id: SupportBundleCategoryId.PERFORMANCE_METRICS,
    recommended: false,
    components: [
      { component: SupportBundleComponentType.PrometheusMetrics, i18nKey: 'prometheusMetrics' },
      {
        component: SupportBundleComponentType.PerfAdvisor,
        i18nKey: 'perfAdvisor',
        requiresPerfAdvisor: true
      }
    ]
  },
  {
    id: SupportBundleCategoryId.SECURITY_AUDIT,
    recommended: false,
    components: [{ component: PG_AUDIT_LOGS, i18nKey: 'pgAuditLogs', sensitive: true }]
  }
];

export interface SupportBundleUniverseContext {
  isK8sUniverse: boolean;
  /** PA metrics can only be collected once Perf Advisor collection is enabled (PLAT-22797). */
  isPerfAdvisorRegistered: boolean;
}

const isComponentAvailable = (
  def: SupportBundleComponentDef,
  context: SupportBundleUniverseContext
) =>
  (!def.k8sOnly || context.isK8sUniverse) &&
  (!def.vmOnly || !context.isK8sUniverse) &&
  (!def.requiresPerfAdvisor || context.isPerfAdvisorRegistered);

/** Categories with the components that apply to this universe; empty categories are dropped. */
export const getAvailableCategories = (
  context: SupportBundleUniverseContext
): SupportBundleCategoryDef[] =>
  SUPPORT_BUNDLE_CATEGORIES.map((category) => ({
    ...category,
    components: category.components.filter((def) => isComponentAvailable(def, context))
  })).filter((category) => category.components.length > 0);

export const getCategoryComponents = (
  categoryId: SupportBundleCategoryId,
  context: SupportBundleUniverseContext
): SupportBundleSelectableComponent[] =>
  getAvailableCategories(context)
    .find((category) => category.id === categoryId)
    ?.components.map((def) => def.component) ?? [];

export const getRecommendedComponents = (
  context: SupportBundleUniverseContext
): SupportBundleSelectableComponent[] =>
  getAvailableCategories(context)
    .filter((category) => category.recommended)
    .flatMap((category) => category.components.map((def) => def.component));

/** Takes a plain string because estimate responses key sizes by component name. */
export const getCategoryForComponent = (component: string): SupportBundleCategoryId | undefined =>
  SUPPORT_BUNDLE_CATEGORIES.find((category) =>
    category.components.some((def) => def.component === component)
  )?.id;

export const isSensitiveComponent = (component: SupportBundleSelectableComponent): boolean =>
  SUPPORT_BUNDLE_CATEGORIES.some((category) =>
    category.components.some((def) => def.component === component && def.sensitive)
  );
