// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Tab } from 'react-bootstrap';
import PropTypes from 'prop-types';
import _ from 'lodash';
import { Link } from 'react-router';
import { Box } from '@material-ui/core';

import { GraphPanelHeaderContainer } from '../../metrics';
import {
  MetricOrigin,
  MetricTypes,
  MetricTypesWithOperations,
  MetricTypesByOrigin,
  MetricMeasure,
  APIMetricToNodeFlag,
  MetricConsts,
  NodeType
} from '../../metrics/constants';
import { YBTabsPanel } from '../../panels';
import { GraphTab } from '../GraphTab/GraphTab';
import { showOrRedirect } from '../../../utils/LayoutUtils';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';

import './CustomerMetricsPanel.scss';

/**
 * Mapping api specific metrics to its nodeDetailsSet flag
 */

/**
 * Move this logic out of render function because we need `selectedUniverse` prop
 * that gets passed down from the `GraphPanelHeader` component.
 */
const PanelBody = ({
  origin,
  selectedUniverse,
  nodePrefixes,
  width,
  tableName,
  graph,
  customer,
  printMode
}) => {
  let result = null;
  const runtimeConfigs = customer?.runtimeConfigs;
  const isGranularMetricsEnabled =
    runtimeConfigs?.data?.configEntries?.find(
      (c) => c.key === 'yb.ui.feature_flags.granular_metrics'
    )?.value === 'true';
  const invalidTabType = [];
  const isYSQLOpsEnabled = selectedUniverse?.universeDetails?.clusters?.[0]?.userIntent.enableYSQL;
  // List of default tabs to display based on metrics origin
  let defaultTabToDisplay = isYSQLOpsEnabled ? MetricTypes.YSQL_OPS : MetricTypes.YCQL_OPS;

  if (origin === MetricOrigin.TABLE) {
    defaultTabToDisplay = MetricTypes.LSMDB_TABLE;
  } else if (origin === MetricOrigin.CUSTOMER) {
    if (selectedUniverse && isKubernetesUniverse(selectedUniverse)) {
      defaultTabToDisplay = MetricTypes.CONTAINER;
    } else {
      defaultTabToDisplay = MetricTypes.SERVER;
    }
  }

  const metricMeasure = graph?.graphFilter?.metricMeasure;
  const currentSelectedNodeType = graph?.graphFilter?.currentSelectedNodeType;
  if (
    metricMeasure === MetricMeasure.OUTLIER ||
    selectedUniverse === MetricConsts.ALL ||
    metricMeasure === MetricMeasure.OVERALL
  ) {
    invalidTabType.push(MetricTypes.OUTLIER_TABLES);
  }

  if (!(selectedUniverse === MetricConsts.ALL)) {
    selectedUniverse && isKubernetesUniverse(selectedUniverse)
      ? invalidTabType.push(MetricTypes.SERVER, MetricTypes.DISK_IO)
      : invalidTabType.push(MetricTypes.CONTAINER);
  }

  if (currentSelectedNodeType !== NodeType.ALL && origin !== MetricOrigin.TABLE) {
    currentSelectedNodeType === NodeType.MASTER
      ? invalidTabType.push(
          MetricTypes.TSERVER,
          MetricTypes.YSQL_OPS,
          MetricTypes.YCQL_OPS,
          MetricTypes.DISK_IO
        )
      : invalidTabType.push(MetricTypes.MASTER, MetricTypes.MASTER_ADVANCED);
    defaultTabToDisplay =
      currentSelectedNodeType === NodeType.MASTER ? MetricTypes.MASTER : MetricTypes.TSERVER;
  }

  const isDrEnabled =
    runtimeConfigs?.data?.configEntries?.find(
      (config) => config.key === RuntimeConfigKey.DISASTER_RECOVERY_FEATURE_FLAG
    )?.value === 'true';
  const drConfigUuid =
    selectedUniverse?.drConfigUuidsAsSource?.[0] ?? selectedUniverse?.drConfigUuidsAsTarget?.[0];
  const hasDrConfig = !!drConfigUuid;
  if (
    metricMeasure === MetricMeasure.OUTLIER ||
    metricMeasure === MetricMeasure.OVERALL ||
    origin === MetricOrigin.TABLE
  ) {
    const metricTabs = MetricTypesByOrigin[origin].data.reduce((prevTabs, type, idx) => {
      const tabTitle = MetricTypesWithOperations[type].title;
      const metricContent = MetricTypesWithOperations[type];
      if (
        !_.includes(Object.keys(APIMetricToNodeFlag), type) ||
        selectedUniverse?.universeDetails?.nodeDetailsSet.some(
          (node) => node[APIMetricToNodeFlag[type]]
        )
      ) {
        if (!invalidTabType.includes(type)) {
          if (printMode) {
            prevTabs.push(
              <div style={{ marginBottom: '20px' }}>
                <GraphTab
                  type={type}
                  metricsKey={metricContent.metrics}
                  nodePrefixes={nodePrefixes}
                  selectedUniverse={selectedUniverse}
                  title={metricContent.title}
                  width={width}
                  tableName={tableName}
                  isGranularMetricsEnabled={isGranularMetricsEnabled}
                  printMode={printMode}
                />
              </div>
            );
          } else {
            prevTabs.push(
              <Tab
                eventKey={type}
                title={tabTitle}
                key={`${type}-${metricMeasure}-tab`}
                mountOnEnter={true}
                unmountOnExit={true}
              >
                <GraphTab
                  type={type}
                  metricsKey={metricContent.metrics}
                  nodePrefixes={nodePrefixes}
                  selectedUniverse={selectedUniverse}
                  title={metricContent.title}
                  width={width}
                  tableName={tableName}
                  isGranularMetricsEnabled={isGranularMetricsEnabled}
                />
              </Tab>
            );
          }
        }
      }
      return prevTabs;
    }, []);
    if (origin === MetricOrigin.UNIVERSE && isDrEnabled && hasDrConfig) {
      if (printMode) {
        metricTabs.push(
          <Box marginTop="16px" textAlign="center">
            <Link to={`/universes/${selectedUniverse.universeUUID}/recovery`}>
              <span className="dr-metrics-link">See xCluster DR Metrics</span>
            </Link>
          </Box>
        );
      } else {
        metricTabs.push(
          <Tab
            eventKey={'xClusterDr'}
            title={'xCluster DR'}
            key={`xClusterDr-${metricMeasure}-tab`}
            mountOnEnter={true}
            unmountOnExit={true}
          >
            <Box marginTop="16px" textAlign="center">
              <Link to={`/universes/${selectedUniverse.universeUUID}/recovery`}>
                <span className="dr-metrics-link">See xCluster DR Metrics</span>
              </Link>
            </Box>
          </Tab>
        );
      }
    }
    if (printMode) {
      result = <div id="print-metrics">{metricTabs}</div>;
    } else {
      result = (
        <YBTabsPanel defaultTab={defaultTabToDisplay} className="overall-metrics-by-origin">
          {metricTabs}
        </YBTabsPanel>
      );
    }
  } else if (metricMeasure === MetricMeasure.OUTLIER_TABLES) {
    if (printMode) {
      result = (
        <div id="print-metrics" style={{ marginBottom: '20px' }}>
          <GraphTab
            type={MetricTypes.OUTLIER_TABLES}
            metricsKey={MetricTypesWithOperations[MetricTypes.OUTLIER_TABLES].metrics}
            nodePrefixes={nodePrefixes}
            selectedUniverse={selectedUniverse}
            title={MetricTypesWithOperations[MetricTypes.OUTLIER_TABLES].title}
            width={width}
            tableName={tableName}
            isGranularMetricsEnabled={isGranularMetricsEnabled}
            printMode={printMode}
          />
        </div>
      );
    } else {
      result = (
        <YBTabsPanel
          defaultTab={MetricTypes.OUTLIER_TABLES}
          activeTab={MetricTypes.OUTLIER_TABLES}
          className="overall-metrics-by-origin"
        >
          <Tab
            eventKey={MetricTypes.OUTLIER_TABLES}
            title={MetricTypesWithOperations[MetricTypes.OUTLIER_TABLES].title}
            key={`${MetricTypes.OUTLIER_TABLES}-tab`}
            mountOnEnter={true}
            unmountOnExit={true}
          >
            <GraphTab
              type={MetricTypes.OUTLIER_TABLES}
              metricsKey={MetricTypesWithOperations[MetricTypes.OUTLIER_TABLES].metrics}
              nodePrefixes={nodePrefixes}
              selectedUniverse={selectedUniverse}
              title={MetricTypesWithOperations[MetricTypes.OUTLIER_TABLES].title}
              width={width}
              tableName={tableName}
              isGranularMetricsEnabled={isGranularMetricsEnabled}
            />
          </Tab>
        </YBTabsPanel>
      );
    }
  }

  return result;
};

export default class CustomerMetricsPanel extends Component {
  static propTypes = {
    origin: PropTypes.oneOf([MetricOrigin.CUSTOMER, MetricOrigin.UNIVERSE, MetricOrigin.TABLE])
      .isRequired,
    nodePrefixes: PropTypes.array,
    width: PropTypes.number,
    tableName: PropTypes.string
  };

  static defaultProps = {
    nodePrefixes: [],
    tableName: null,
    width: null
  };

  componentDidMount() {
    const {
      customer: { currentCustomer }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.metrics');
  }

  render() {
    const { origin, printMode = false } = this.props;
    return (
      <GraphPanelHeaderContainer origin={origin} printMode={printMode}>
        <PanelBody {...this.props} printMode={printMode} />
      </GraphPanelHeaderContainer>
    );
  }
}
