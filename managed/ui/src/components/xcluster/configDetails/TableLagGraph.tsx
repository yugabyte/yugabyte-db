import _ from 'lodash';
import moment from 'moment';
import { FC, useState } from 'react';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';

import { getAlertConfigurations } from '../../../actions/universe';
import { queryLagMetricsForTable } from '../../../actions/xClusterReplication';
import { YBButtonLink } from '../../common/forms/fields';
import { YBErrorIndicator } from '../../common/indicators';
import { MetricsPanelOld } from '../../metrics';
import { CustomDatePicker } from '../../metrics/CustomDatePicker/CustomDatePicker';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  MetricName,
  METRIC_TIME_RANGE_OPTIONS,
  REPLICATION_LAG_ALERT_NAME,
  TABLE_LAG_GRAPH_EMPTY_METRIC,
  TIME_RANGE_TYPE
} from '../constants';

import {
  MetricTimeRange,
  MetricTimeRangeOption,
  StandardMetricTimeRangeOption,
  Metrics,
  XClusterTable
} from '../XClusterTypes';

import styles from './TableLagGraph.module.scss';
import { getMaxNodeLagMetric } from '../ReplicationUtils';

const TABLE_LAG_METRICS_REFETCH_INTERVAL = 60_000;
const GRAPH_WIDTH = 850;
const GRAPH_HEIGHT = 600;

const getTimeRange = (metricTimeRangeOption: StandardMetricTimeRangeOption): MetricTimeRange => {
  return {
    startMoment: moment().subtract(metricTimeRangeOption.value, metricTimeRangeOption.type),
    endMoment: moment()
  };
};

interface Props {
  tableDetails: XClusterTable;
  replicationUUID: string;
  universeUUID: string;
  queryEnabled: boolean;
  nodePrefix: string;
}

export const TableLagGraph: FC<Props> = ({
  tableDetails: { tableName, tableUUID, streamId },
  universeUUID,
  queryEnabled,
  nodePrefix
}) => {
  const { currentUser } = useSelector((state: any) => state.customer);
  const { prometheusQueryEnabled } = useSelector((state: any) => state.graph);

  const [selectedTimeRangeOption, setSelectedTimeRangeOption] = useState<MetricTimeRangeOption>(
    DEFAULT_METRIC_TIME_RANGE_OPTION
  );

  const [customStartMoment, setCustomStartMoment] = useState(
    getTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).startMoment
  );
  const [customEndMoment, setCustomEndMoment] = useState(
    getTimeRange(DEFAULT_METRIC_TIME_RANGE_OPTION).endMoment
  );

  const tableMetricsQuery = useQuery(
    ['xClusterMetric', nodePrefix, tableUUID, streamId, selectedTimeRangeOption],
    () => {
      if (selectedTimeRangeOption.type === TIME_RANGE_TYPE.CUSTOM) {
        return queryLagMetricsForTable(
          streamId,
          tableUUID,
          nodePrefix,
          customStartMoment.format('X'),
          customEndMoment.format('X')
        );
      }

      const timeRange = getTimeRange(selectedTimeRangeOption);

      return queryLagMetricsForTable(
        streamId,
        tableUUID,
        nodePrefix,
        timeRange.startMoment.format('X'),
        timeRange.endMoment.format('X')
      );
    },
    {
      enabled: queryEnabled && !!nodePrefix,
      refetchInterval: TABLE_LAG_METRICS_REFETCH_INTERVAL
    }
  );

  const configurationFilter = {
    name: REPLICATION_LAG_ALERT_NAME,
    targetUuid: universeUUID
  };

  const alertConfigQuery = useQuery(['alert', 'configurations', configurationFilter], () =>
    getAlertConfigurations(configurationFilter)
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

  const graphMetric = _.cloneDeep(tableMetricsQuery.data ?? TABLE_LAG_GRAPH_EMPTY_METRIC);
  setTracesToPlot(graphMetric);

  return (
    <div>
      <div className={styles.modalToolBar}>
        <YBButtonLink
          btnIcon={'fa fa-refresh'}
          btnClass="btn btn-default refresh-btn"
          disabled={selectedTimeRangeOption.type === TIME_RANGE_TYPE.CUSTOM}
          onClick={tableMetricsQuery.refetch}
        />
        {selectedTimeRangeOption.type === TIME_RANGE_TYPE.CUSTOM && (
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
        metricKey={`${MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC}_${tableName}`}
        metric={_.cloneDeep(graphMetric.tserver_async_replication_lag_micros)}
        width={GRAPH_WIDTH}
        height={GRAPH_HEIGHT}
        shouldAbbreviateTraceName={false}
        prometheusQueryEnabled={prometheusQueryEnabled}
      />
    </div>
  );
};
