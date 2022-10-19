// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Tab, PanelGroup } from 'react-bootstrap';
import { browserHistory } from 'react-router';
import PropTypes from 'prop-types';
import _ from 'lodash';

import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics';
import {
  MetricOrigin,
  MetricTypes,
  MetricTypesWithOperations,
  MetricTypesByOrigin,
  APIMetricToNodeFlag
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
const PanelBody = ({ origin,
  selectedUniverse,
  nodePrefixes,
  width,
  tableName,
  featureFlags
}) => {
  const isTopKMetricsEnabled = featureFlags.test.enableTopKMetrics || featureFlags.released.enableTopKMetrics;
  let result = null;

  if (isTopKMetricsEnabled) {
    // List of default tabs to display based on metrics origin
    let defaultTabToDisplay = MetricTypes.YSQL_OPS;
    if (origin === MetricOrigin.TABLE) {
      defaultTabToDisplay = MetricTypes.LSMDB_TABLE;
    } else if (origin === MetricOrigin.CUSTOMER) {
      defaultTabToDisplay = MetricTypes.SERVER;
    }

    result = (
      <YBTabsPanel
        defaultTab={defaultTabToDisplay}
        id="overall-metrics"
      >
        {MetricTypesByOrigin[origin].data.reduce((prevTabs, type, idx) => {
          const invalidTabType =
            selectedUniverse && isKubernetesUniverse(selectedUniverse)
              ? MetricTypes.SERVER
              : MetricTypes.CONTAINER;
          const tabTitle = MetricTypesWithOperations[type].title;
          const metricContent = MetricTypesWithOperations[type];
          if (
            !_.includes(Object.keys(APIMetricToNodeFlag), type) ||
            selectedUniverse?.universeDetails?.nodeDetailsSet.some(
              (node) => node[APIMetricToNodeFlag[type]]
            )
          ) {
            if (!(invalidTabType === type)) {
              prevTabs.push(
                <Tab
                  eventKey={type}
                  title={tabTitle}
                  key={`${type}-tab`}
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
                  />
                </Tab>
              );
            }
          }
          return prevTabs;
        }, [])}
      </YBTabsPanel>
    );
    // TODO: Needs to be removed once Top K metrics is tested and integrated fully
  } else {
    const location = browserHistory.getCurrentLocation();
    const currentQuery = location.query;

    result = (
      <PanelGroup id={origin + ' metrics'}>
        {MetricTypesByOrigin[origin].data.reduce((prevPanels, type, idx) => {
          // if we have subtab query param, then we would have that metric tab open by default
          const isOpen = currentQuery.subtab
            ? type === currentQuery.subtab
            : MetricTypesByOrigin[origin].isOpen[idx];

          if (
            !_.includes(Object.keys(APIMetricToNodeFlag), type) ||
            selectedUniverse?.universeDetails?.nodeDetailsSet.some(
              (node) => node[APIMetricToNodeFlag[type]]
            )
          ) {
            prevPanels.push(
              <GraphPanelContainer
                key={idx}
                isOpen={isOpen}
                type={type}
                width={width}
                nodePrefixes={nodePrefixes}
                tableName={tableName}
                selectedUniverse={selectedUniverse}
              />
            );
          }
          return prevPanels;
        }, [])}
      </PanelGroup>
    );
  }

  return result;
};

export default class CustomerMetricsPanel extends Component {
  static propTypes = {
    origin: PropTypes.oneOf(
      [
        MetricOrigin.CUSTOMER,
        MetricOrigin.UNIVERSE,
        MetricOrigin.TABLE
      ]).isRequired,
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
