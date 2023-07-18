// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import PropTypes from 'prop-types';
import moment from 'moment';
import _, { isEmpty } from 'lodash';
import { isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBPanelItem } from '../../panels';
import { YBLoading } from '../../common/indicators';
import { YBResourceCount } from '../../common/descriptors';
import { MetricsPanelOld } from '../../metrics';
import { ReplicationAlertModalBtn } from './ReplicationAlertModalBtn';
import { Dropdown, MenuItem } from 'react-bootstrap';
import { CustomDatePicker } from '../../metrics/CustomDatePicker/CustomDatePicker';
import {
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  METRIC_TIME_RANGE_OPTIONS
} from '../../xcluster/constants';

import './Replication.scss';

const GRAPH_TYPE = 'replication';
const METRIC_NAME = 'tserver_async_replication_lag_micros';
const MILLI_IN_MIN = 60000.0;
const MILLI_IN_SEC = 1000.0;

const {
  type: DEFAULT_METRIC_TIME_RANGE_TYPE,
  value: DEFAULT_METRIC_TIME_RANGE_VALUE,
  label: DEFAULT_METRIC_TIME_RANGE_LABEL
} = DEFAULT_METRIC_TIME_RANGE_OPTION;

export default class Replication extends Component {
  constructor(props) {
    super(props);

    this.state = {
      graphWidth: props.hideHeader ? window.innerWidth - 300 : 840,
      intervalId: null,
      filterLabel: DEFAULT_METRIC_TIME_RANGE_LABEL,
      filterType: DEFAULT_METRIC_TIME_RANGE_TYPE,
      filterValue: DEFAULT_METRIC_TIME_RANGE_VALUE,
      startMoment: moment().subtract(
        DEFAULT_METRIC_TIME_RANGE_VALUE,
        DEFAULT_METRIC_TIME_RANGE_TYPE
      ),
      endMoment: moment()
    };
  }

  static propTypes = {
    universe: PropTypes.object.isRequired
  };

  componentDidMount() {
    const { sourceUniverseUUID } = this.props;
    if (sourceUniverseUUID) {
      this.props.fetchCurrentUniverse(sourceUniverseUUID).then(() => {
        this.queryMetrics();
      });
    } else {
      this.queryMetrics();
    }
    const intervalId = setInterval(() => {
      this.queryMetrics();
    }, 10 * 2000);
    this.setState({
      intervalId
    });
  }

  componentWillUnmount() {
    this.props.resetMasterLeader();
    clearInterval(this.state.intervalId);
  }

  queryMetrics = () => {
    const {
      universe: { currentUniverse },
      replicationUUID
    } = this.props;
    const { startMoment, endMoment, filterValue, filterType, filterLabel } = this.state;
    const universeDetails = getPromiseState(currentUniverse).isSuccess()
      ? currentUniverse.data.universeDetails
      : 'all';

    const params = {
      metrics: [METRIC_NAME],
      start: startMoment.format('X'),
      end: endMoment.format('X'),
      nodePrefix: universeDetails.nodePrefix,
      xClusterConfigUuid: replicationUUID
    };

    if (filterLabel !== 'Custom') {
      params['start'] = moment().subtract(filterValue, filterType).format('X');
      params['end'] = moment().format('X');
    }

    this.props.queryMetrics(params, GRAPH_TYPE);
  };

  handleFilterChange = (eventKey, event) => {
    const filterInfo = METRIC_TIME_RANGE_OPTIONS[eventKey];
    const self = this;

    let stateToUpdate = {
      filterLabel: filterInfo.label,
      filterType: filterInfo.type,
      filterValue: filterInfo.value
    };
    if (event.target.getAttribute('data-filter-type') !== 'custom') {
      stateToUpdate = {
        ...stateToUpdate,
        endMoment: moment(),
        startMoment: moment().subtract(filterInfo.value, filterInfo.type)
      };
      this.setState(stateToUpdate, () => self.queryMetrics());
    } else {
      this.setState(stateToUpdate);
    }
  };

  handleStartDateChange = (dateStr) => {
    this.setState({ startMoment: moment(dateStr) });
  };

  handleEndDateChange = (dateStr) => {
    this.setState({ endMoment: moment(dateStr) });
  };
  render() {
    const {
      universe: { currentUniverse },
      graph: { metrics, prometheusQueryEnabled },
      customer: { currentUser },
      hideHeader
    } = this.props;
    if (isEmpty(currentUniverse.data)) {
      return <YBLoading />;
    }
    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    const universePaused = currentUniverse?.data?.universeDetails?.universePaused;
    if (!isNonEmptyArray(nodeDetails)) {
      return <YBLoading />;
    }
    let latestStat = null;
    let latestTimestamp = null;
    let showMetrics = false;
    let aggregatedMetrics = {};
    if (_.get(metrics, `${GRAPH_TYPE}.${METRIC_NAME}.layout.yaxis.alias`, null)) {
      // Get alias
      const metricAliases = metrics[GRAPH_TYPE][METRIC_NAME].layout.yaxis.alias;
      const committedLagName = metricAliases['async_replication_committed_lag_micros'];
      aggregatedMetrics = { ...metrics[GRAPH_TYPE][METRIC_NAME] };
      const replicationNodeMetrics = metrics[GRAPH_TYPE][METRIC_NAME].data
        .filter((x) => x.name === committedLagName)
        .sort((a, b) => b.x.length - a.x.length);

      if (replicationNodeMetrics.length > 0) {
        // Get max-value and avg-value metric array
        let avgArr = null;
        let maxArr = null;
        replicationNodeMetrics.forEach((metric) => {
          if (!avgArr && !maxArr) {
            avgArr = metric.y.map((v) => parseFloat(v) / replicationNodeMetrics.length);
            maxArr = [...metric.y];
          } else {
            metric.y.forEach((y, idx) => {
              avgArr[idx] = parseFloat(avgArr[idx]) + parseFloat(y) / replicationNodeMetrics.length;
              if (parseFloat(y) > parseFloat(maxArr[idx])) {
                maxArr[idx] = parseFloat(y);
              }
            });
          }
        });
        const firstMetricData = replicationNodeMetrics[0];
        aggregatedMetrics.data = [
          {
            ...firstMetricData,
            name: `Max ${committedLagName}`,
            y: maxArr
          },
          {
            ...firstMetricData,
            name: `Avg ${committedLagName}`,
            y: avgArr
          }
        ];
        latestStat = avgArr[avgArr.length - 1];
        latestTimestamp = firstMetricData.x[firstMetricData.x.length - 1];
        showMetrics = true;
      }
    }

    let infoBlock = <span />;
    let recentStatBlock = null;
    // eslint-disable-next-line eqeqeq
    if (latestStat != null) {
      if (parseInt(latestStat) === 0) {
        infoBlock = (
          <div className="info success">
            <i className="fa fa-check-circle icon"></i>
            <div>
              <h4>Cluster is caught up!</h4>
            </div>
          </div>
        );
      }
      let resourceNumber = (
        <YBResourceCount size={parseFloat(latestStat.toFixed(4))} kind="ms" inline={true} />
      );
      if (latestStat > MILLI_IN_MIN) {
        resourceNumber = (
          <YBResourceCount size={(latestStat / MILLI_IN_MIN).toFixed(4)} kind="min" inline={true} />
        );
      } else if (latestStat > MILLI_IN_SEC) {
        resourceNumber = (
          <YBResourceCount size={(latestStat / MILLI_IN_SEC).toFixed(4)} kind="s" inline={true} />
        );
      }
      recentStatBlock = (
        <div className="metric-block">
          <h3>Current Replication Lag</h3>
          {resourceNumber}
          <div className="metric-attribute">as of {moment(latestTimestamp).fromNow()}</div>
        </div>
      );
    }

    let datePicker = null;
    if (this.state.filterLabel === 'Custom') {
      datePicker = (
        <CustomDatePicker
          startMoment={this.state.startMoment}
          endMoment={this.state.endMoment}
          setStartMoment={this.handleStartDateChange}
          setEndMoment={this.handleEndDateChange}
          handleTimeframeChange={this.queryMetrics}
        />
      );
    }

    const self = this;

    const menuItems = METRIC_TIME_RANGE_OPTIONS.map((filter, idx) => {
      const key = 'graph-filter-' + idx;
      if (filter.type === 'divider') {
        return <MenuItem divider key={key} />;
      }

      return (
        <MenuItem
          onSelect={self.handleFilterChange}
          data-filter-type={filter.type}
          key={key}
          eventKey={idx}
          active={filter.label === self.state.filterLabel}
        >
          {filter.label}
        </MenuItem>
      );
    });

    // TODO: Make graph resizeable
    return (
      <div id="replication-tab-panel">
        <YBPanelItem
          header={
            <div className="replication-header">
              <h2>Replication</h2>
              {!universePaused && !hideHeader && (
                <ReplicationAlertModalBtn
                  universeUUID={currentUniverse.data.universeUUID}
                  disabled={!showMetrics}
                />
              )}
            </div>
          }
          body={
            <div className="replication-content">
              {!hideHeader && infoBlock}
              {!hideHeader && <div className="replication-content-stats">{recentStatBlock}</div>}
              {!showMetrics && <div className="no-data">No data to display.</div>}
              {showMetrics && (
                <div className={`time-range-option ${!hideHeader ? 'old-view' : ''}`}>
                  {datePicker}
                  <Dropdown id="graphFilterDropdown" className="graph-filter-dropdown" pullRight>
                    <Dropdown.Toggle>
                      <i className="fa fa-clock-o"></i>&nbsp;
                      {this.state.filterLabel}
                    </Dropdown.Toggle>
                    <Dropdown.Menu>{menuItems}</Dropdown.Menu>
                  </Dropdown>
                </div>
              )}

              {showMetrics && metrics[GRAPH_TYPE] && (
                <div className={`graph-container ${!hideHeader ? 'old-view' : ''}`}>
                  <MetricsPanelOld
                    currentUser={currentUser}
                    metricKey={METRIC_NAME}
                    metric={JSON.parse(JSON.stringify(aggregatedMetrics))}
                    className={'metrics-panel-container'}
                    width={this.state.graphWidth}
                    height={540}
                    prometheusQueryEnabled={prometheusQueryEnabled}
                    shouldAbbreviateTraceName={false}
                  />
                </div>
              )}
            </div>
          }
        />
      </div>
    );
  }
}
