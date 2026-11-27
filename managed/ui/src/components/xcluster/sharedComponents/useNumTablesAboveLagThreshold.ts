import moment from 'moment';
import { useQuery } from 'react-query';

import { getAlertConfigurations } from '../../../actions/universe';
import { AlertTemplate } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';
import { alertConfigQueryKey, api, metricQueryKey } from '../../../redesign/helpers/api';
import { MetricsQueryParams, Universe } from '../../../redesign/helpers/dtos';
import { NodeAggregation, SplitType } from '../../metrics/dtos';
import { liveMetricTimeRangeUnit, liveMetricTimeRangeValue, MetricName } from '../constants';
import { getStrictestReplicationLagAlertThreshold } from '../ReplicationUtils';

/**
 * Counts tables whose latest replication lag is above the strictest replication lag alert
 * threshold on the source universe. Returns 0 until both the alert config and metric queries
 * succeed.
 */
export const useNumTablesAboveLagThreshold = (
  sourceUniverse: Universe | undefined,
  xClusterConfigUuid: string
): number => {
  const alertConfigFilter = {
    template: AlertTemplate.REPLICATION_LAG,
    targetUuid: sourceUniverse?.universeUUID
  };
  const replicationLagAlertConfigQuery = useQuery(
    alertConfigQueryKey.list(alertConfigFilter),
    () => getAlertConfigurations(alertConfigFilter),
    {
      enabled: !!sourceUniverse
    }
  );

  const replicationLagMetricRequestParams: MetricsQueryParams = {
    metricsWithSettings: [
      {
        metric: MetricName.ASYNC_REPLICATION_SENT_LAG,
        nodeAggregation: NodeAggregation.MAX,
        splitType: SplitType.TABLE
      }
    ],
    nodePrefix: sourceUniverse?.universeDetails.nodePrefix,
    xClusterConfigUuid,
    start: moment().subtract(liveMetricTimeRangeValue, liveMetricTimeRangeUnit).format('X'),
    end: moment().format('X')
  };
  const tableReplicationLagQuery = useQuery(
    metricQueryKey.live(
      replicationLagMetricRequestParams,
      liveMetricTimeRangeValue,
      liveMetricTimeRangeUnit
    ),
    () => api.fetchMetrics(replicationLagMetricRequestParams),
    {
      enabled: !!sourceUniverse
    }
  );

  if (!replicationLagAlertConfigQuery.isSuccess || !tableReplicationLagQuery.isSuccess) {
    return 0;
  }
  const maxAcceptableLag = getStrictestReplicationLagAlertThreshold(
    replicationLagAlertConfigQuery.data
  );
  if (!maxAcceptableLag) {
    return 0;
  }
  return (
    tableReplicationLagQuery.data.async_replication_sent_lag?.data.filter((trace) => {
      const latestLag = trace.y[trace.y.length - 1];
      return latestLag && latestLag > maxAcceptableLag;
    }).length ?? 0
  );
};
