import { FC, useState } from 'react';
import moment from 'moment-timezone';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { CartesianGrid, Line, LineChart, ReferenceLine, Tooltip, XAxis, YAxis } from 'recharts';

import { getAlertConfigurations } from '../../../actions/universe';
import { queryLagMetricsForUniverse } from '../../../actions/xClusterReplication';
import { api } from '../../../redesign/helpers/api';
import { getStrictestReplicationLagAlertThreshold } from '../ReplicationUtils';

import './LagGraph.scss';

const ALERT_NAME = 'Replication Lag';

const METRIC_NAME = 'tserver_async_replication_lag_micros';

// TODO: Decide whether we need this component.
// JIRA: https://yugabyte.atlassian.net/browse/PLAT-5708
interface LagGraphProps {
  replicationUUID: string;
  sourceUniverseUUID: string;
}
export const LagGraph: FC<LagGraphProps> = ({ replicationUUID, sourceUniverseUUID }) => {
  const [maxConfiguredLag, setMaxConfiguredLag] = useState(0);
  const currentUserTimezone = useSelector((state: any) => state.customer.currentUser.data.timezone);
  const { data: universeInfo, isLoading: currentUniverseLoading } = useQuery(
    ['universe', sourceUniverseUUID],
    () => api.fetchUniverse(sourceUniverseUUID)
  );

  const nodePrefix = universeInfo?.universeDetails.nodePrefix;

  const { data: metrics } = useQuery(
    ['xcluster-metric', replicationUUID, nodePrefix, 'metric'],
    () => queryLagMetricsForUniverse(nodePrefix, replicationUUID),
    {
      enabled: !currentUniverseLoading
    }
  );

  const configurationFilter = {
    name: ALERT_NAME,
    targetUuid: sourceUniverseUUID
  };

  useQuery(
    ['getAlertConfigurations', configurationFilter],
    () => getAlertConfigurations(configurationFilter),
    {
      onSuccess: (data) => {
        const strictestReplicationLagAlertThreshold = getStrictestReplicationLagAlertThreshold(
          data
        );
        if (strictestReplicationLagAlertThreshold) {
          setMaxConfiguredLag(strictestReplicationLagAlertThreshold);
        }
      }
    }
  );

  if (
    !metrics?.tserver_async_replication_lag_micros ||
    !Array.isArray(metrics.tserver_async_replication_lag_micros.data)
  ) {
    return null;
  }

  const metricAliases = metrics[METRIC_NAME].layout.yaxis.alias;
  const committedLagName = metricAliases['async_replication_committed_lag_micros'];

  const replicationNodeMetrics = metrics[METRIC_NAME].data.filter(
    (x: any) => x.name === committedLagName
  );

  let maxLagInMetric = 0.0;

  const graphData: any = [];

  replicationNodeMetrics.forEach((nodeMetric: any) => {
    nodeMetric.x.forEach((xAxis: any, index: number) => {
      const parsedY = parseFloat(nodeMetric.y[index]) || 0;
      if (parsedY > maxLagInMetric) {
        maxLagInMetric = parsedY;
      }

      const momentObj = currentUserTimezone
        ? (moment(xAxis) as any).tz(currentUserTimezone)
        : moment(xAxis);
      graphData[index] = {
        x: momentObj.format('HH:mm'),
        max_lag: graphData[index]
          ? graphData[index].max_lag > parsedY
            ? graphData[index].max_lag
            : parsedY
          : parsedY
      };
    });
  });

  return (
    <LineChart
      width={380}
      height={100}
      data={graphData}
      margin={{ top: 30, right: 30, left: 0, bottom: 0 }}
    >
      <XAxis label="Last Hour" dataKey="x" tick={false} />
      <YAxis
        dataKey="max_lag"
        type="number"
        domain={[0, Math.trunc(maxConfiguredLag)]}
        ticks={[0, Math.trunc(maxConfiguredLag)]}
        tickFormatter={(e) => e + 'ms'}
        allowDataOverflow
      />
      <CartesianGrid strokeDasharray="3 3" fill="#eaf5ec" />
      <ReferenceLine y={Math.trunc(maxConfiguredLag)} stroke="black" strokeDasharray="8 8" />
      <Line dot={false} dataKey="max_lag" stroke="black" fill="black" />
      <Tooltip />
    </LineChart>
  );
};
