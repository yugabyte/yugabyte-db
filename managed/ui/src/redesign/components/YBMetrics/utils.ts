import i18next from 'i18next';

import { SplitMode, SplitType } from '../../../components/metrics/dtos';
import { formatUuidFromXCluster } from '../../../components/xcluster/ReplicationUtils';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { MetricSettings, MetricTrace } from '../../helpers/dtos';

export const getUniqueTraceId = (trace: MetricTrace): string =>
  `${trace.name}.${trace.instanceName}.${trace.namespaceId ?? trace.namespaceName}.${
    trace.tableId ?? trace.tableName
  }`;

export const getUniqueTraceName = (
  metricSettings: MetricSettings,
  trace: MetricTrace,
  namespaceUuidToNamespace: { [namespaceUuid: string]: string | undefined }
): string => {
  const traceName = i18next.t(`prometheusMetricTrace.${trace.name}`);

  if (
    metricSettings.splitMode !== SplitMode.NONE &&
    !(
      trace.instanceName ||
      trace.namespaceId ||
      trace.namespaceName ||
      trace.tableId ||
      trace.tableName
    )
  ) {
    // If the we're showing top/bottom k and there is a trace with no extra metadata, then
    // we assume this is the average trace.
    return `${traceName} (Average)`;
  }
  switch (metricSettings.splitType) {
    case undefined:
    case SplitType.NONE:
      return traceName;
    case SplitType.NODE:
      return `${traceName} (${trace.instanceName})`;
    case SplitType.NAMESPACE: {
      const namespaceName =
        trace.namespaceName ??
        namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '')];
      return namespaceName ? `${traceName} (${namespaceName})` : traceName;
    }
    case SplitType.TABLE: {
      const namespaceName =
        trace.namespaceName ??
        namespaceUuidToNamespace[formatUuidFromXCluster(trace.namespaceId ?? '<unknown>')];
      const tableIdentifier = trace.tableName
        ? `${namespaceName}/${trace.tableName}`
        : namespaceName;
      return `${traceName} (${tableIdentifier})`;
    }
    default:
      return assertUnreachableCase(metricSettings.splitType);
  }
};
