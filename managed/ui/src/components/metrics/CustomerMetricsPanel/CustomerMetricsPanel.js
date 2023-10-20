// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Tab } from 'react-bootstrap';
import PropTypes from 'prop-types';
import _ from 'lodash';

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
  customer
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
      ? invalidTabType.push(MetricTypes.SERVER)
      : invalidTabType.push(MetricTypes.CONTAINER);
  }

  if (currentSelectedNodeType !== NodeType.ALL && origin !== MetricOrigin.TABLE) {
    currentSelectedNodeType === NodeType.MASTER
      ? invalidTabType.push(MetricTypes.TSERVER, MetricTypes.YSQL_OPS, MetricTypes.YCQL_OPS)
      : invalidTabType.push(MetricTypes.MASTER, MetricTypes.MASTER_ADVANCED);
    defaultTabToDisplay =
      currentSelectedNodeType === NodeType.MASTER ? MetricTypes.MASTER : MetricTypes.TSERVER;
  }

  if (
    metricMeasure === MetricMeasure.OUTLIER ||
    metricMeasure === MetricMeasure.OVERALL ||
    origin === MetricOrigin.TABLE
  ) {
    result = (
      <YBTabsPanel defaultTab={defaultTabToDisplay} className="overall-metrics-by-origin">
        {MetricTypesByOrigin[origin].data.reduce((prevTabs, type, idx) => {
          const tabTitle = MetricTypesWithOperations[type].title;
          const metricContent = MetricTypesWithOperations[type];
          if (
            !_.includes(Object.keys(APIMetricToNodeFlag), type) ||
            selectedUniverse?.universeDetails?.nodeDetailsSet.some(
              (node) => node[APIMetricToNodeFlag[type]]
            )
          ) {
            if (!invalidTabType.includes(type)) {
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
          return prevTabs;
        }, [])}
      </YBTabsPanel>
    );
  } else if (metricMeasure === MetricMeasure.OUTLIER_TABLES) {
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
    const { origin } = this.props;
    return (
      <GraphPanelHeaderContainer origin={origin}>
        <PanelBody {...this.props} />
      </GraphPanelHeaderContainer>
    );
  }
}
