import _ from 'lodash';
import moment from 'moment';
import { FC, useState } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';

import { getAlertConfigurations } from '../../../actions/universe';
import { fetchReplicationLag } from '../../../actions/xClusterReplication';
import { alertConfigQueryKey, metricQueryKey } from '../../../redesign/helpers/api';
import { getTableName, getTableUuid } from '../../../utils/tableUtils';
import { YBButtonLink } from '../../common/forms/fields';
import { YBErrorIndicator } from '../../common/indicators';
import { MetricsPanelOld } from '../../metrics';
import { CustomDatePicker } from '../../metrics/CustomDatePicker/CustomDatePicker';
import {
  AlertName,
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  PollingIntervalMs,
  REPLICATION_LAG_GRAPH_EMPTY_METRIC,
  TimeRangeType
} from '../constants';
import { getMaxNodeLagMetric, getMetricTimeRange } from '../ReplicationUtils';

import { MetricTimeRangeOption, Metrics, XClusterTable } from '../XClusterTypes';

import styles from './TableLagGraph.module.scss';

const GRAPH_WIDTH = 850;
const GRAPH_HEIGHT = 600;

interface Props {
  tableDetails: XClusterTable;
  replicationUUID: string;
  universeUUID: string;
  queryEnabled: boolean;
  nodePrefix: string;
}

export const TableLagGraph: FC<Props> = ({
  tableDetails,
  universeUUID,
  queryEnabled,
  nodePrefix
}) => {
  const [selectedTimeRangeOption, setSelectedTimeRangeOption] = useState<MetricTimeRangeOption>(
    DEFAULT_METRIC_TIME_RANGE_OPTION
  );
  const [customStartMoment, setCustomStartMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).startMoment
  );
  const [customEndMoment, setCustomEndMoment] = useState(
    getMetricTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).endMoment
  );
  const { currentUser } = useSelector((state: any) => state.customer);
  const { prometheusQueryEnabled } = useSelector((state: any) => state.graph);
  const isCustomTimeRange = selectedTimeRangeOption.type === TimeRangeType.CUSTOM;
  const metricTimeRange = isCustomTimeRange
    ? { startMoment: customStartMoment, endMoment: customEndMoment }
    : getMetricTimeRange(selectedTimeRangeOption);
  // At the moment, we don't support a custom time range which uses the 'current time' as the end time.
  // Thus, all custom time ranges are fixed.
  const isFixedTimeRange = isCustomTimeRange;
  const replciationLagMetricRequestParams = {
    streamId: tableDetails.streamId,
    tableId: getTableUuid(tableDetails),
    nodePrefix,
    start: metricTimeRange.startMoment.format('X'),
    end: metricTimeRange.endMoment.format('X')
  };
  const tableMetricsQuery = useQuery(
    isFixedTimeRange
      ? metricQueryKey.detail(replciationLagMetricRequestParams)
      : metricQueryKey.latest(
          replciationLagMetricRequestParams,
          selectedTimeRangeOption.value,
          selectedTimeRangeOption.type
        ),
    () => fetchReplicationLag(replciationLagMetricRequestParams),
    {
      enabled: queryEnabled && !!nodePrefix,
      // It is unnecessary to refetch metric traces when the interval is fixed as subsequent
      // queries will return the same data.
      staleTime: isFixedTimeRange ? Infinity : 0,
      refetchInterval: isFixedTimeRange ? false : PollingIntervalMs.XCLUSTER_METRICS
    }
  );

  const alertConfigFilter = {
    name: AlertName.REPLICATION_LAG,
    targetUuid: universeUUID
  };
  const alertConfigQuery = useQuery(alertConfigQueryKey.list(alertConfigFilter), () =>
    getAlertConfigurations(alertConfigFilter)
  );
  const maxAcceptableLag = Math.min(
    ...alertConfigQuery.data.map(
      (alertConfig: any): number => alertConfig.thresholds.SEVERE.threshold
    )
  );

  if (tableMetricsQuery.isError) {
    return <YBErrorIndicator />;
  }

  const handleTimeRangeChange = (eventKey: any) => {
    const selectedOption = METRIC_TIME_RANGE_OPTIONS[eventKey];
    if (selectedOption.type !== 'divider') {
      setSelectedTimeRangeOption(selectedOption);
    }
  };

  /**
   * Look for the traces that we are plotting ({@link METRIC_TRACE_NAME}).
   * If found, then we also try to add a trace for the max acceptable lag.
   * If not found, then we just show no data.
   */
  const setTracesToPlot = (graphMetric: Metrics<'tserver_async_replication_lag_micros'>) => {
    const trace = getMaxNodeLagMetric(graphMetric);

    if (typeof maxAcceptableLag === 'number' && trace) {
      graphMetric.tserver_async_replication_lag_micros.data = [
        trace,
        {
          name: 'Max Acceptable Lag',
          instanceName: trace.instanceName,
          type: 'scatter',
          line: {
            dash: 'dot',
            width: 4
          },
          x: trace.x,
          y: Array(trace.y.length).fill(maxAcceptableLag)
        }
      ];
    } else if (trace) {
      graphMetric.tserver_async_replication_lag_micros.data = [trace];
    } else {
      graphMetric.tserver_async_replication_lag_micros.data = [];
    }
  };

  const menuItems = METRIC_TIME_RANGE_OPTIONS.map((option, idx) => {
    if (option.type === 'divider') {
      return <MenuItem divider key={`${idx}_divider`} />;
    }

    return (
      <MenuItem
        onSelect={handleTimeRangeChange}
        key={`${idx}_${option.label}`}
        eventKey={idx}
        active={option.label === selectedTimeRangeOption?.label}
      >
        {option.label}
      </MenuItem>
    );
  });

  const graphMetric = _.cloneDeep(tableMetricsQuery.data ?? REPLICATION_LAG_GRAPH_EMPTY_METRIC);
  setTracesToPlot(graphMetric);
  const tableName = getTableName(tableDetails);
  return (
    <div>
      <div className={styles.modalToolBar}>
        <YBButtonLink
          btnIcon={'fa fa-refresh'}
          btnClass="btn btn-default refresh-btn"
          disabled={selectedTimeRangeOption.type === TimeRangeType.CUSTOM}
          onClick={tableMetricsQuery.refetch}
        />
        {selectedTimeRangeOption.type === TimeRangeType.CUSTOM && (
          <CustomDatePicker
            startMoment={customStartMoment}
            endMoment={customEndMoment}
            setStartMoment={(dateString: any) => setCustomStartMoment(moment(dateString))}
            setEndMoment={(dateString: any) => setCustomEndMoment(moment(dateString))}
            handleTimeframeChange={tableMetricsQuery.refetch}
          />
        )}
        <Dropdown id={`${tableName}LagGraphTimeRangeDropdown`} pullRight>
          <Dropdown.Toggle>
            <i className="fa fa-clock-o"></i>&nbsp;
            {selectedTimeRangeOption?.label}
          </Dropdown.Toggle>
          <Dropdown.Menu>{menuItems}</Dropdown.Menu>
        </Dropdown>
      </div>

      <MetricsPanelOld
        className={styles.graphContainer}
        currentUser={currentUser}
        metricKey={`${MetricName.TSERVER_ASYNC_REPLICATION_LAG}_${tableName}`}
        metric={_.cloneDeep(graphMetric.tserver_async_replication_lag_micros)}
        width={GRAPH_WIDTH}
        height={GRAPH_HEIGHT}
        shouldAbbreviateTraceName={false}
        prometheusQueryEnabled={prometheusQueryEnabled}
      />
    </div>
  );
};
