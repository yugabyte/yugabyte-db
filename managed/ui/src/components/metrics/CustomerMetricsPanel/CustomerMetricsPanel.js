// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics';
import PropTypes from 'prop-types';
import { PanelGroup } from 'react-bootstrap';
import { browserHistory} from 'react-router';
import { isNonAvailable } from 'utils/LayoutUtils';

const graphPanelTypes = {
  "universe": {
    data: ['proxies', 'container', 'server', 'cql', 'redis', 'tserver', 'lsmdb'],
    isOpen: [true, false, false, false, false, false]
  },
  "customer": {
    data: ['proxies', 'container', 'server', 'cql', 'redis', 'tserver', 'lsmdb'],
    isOpen: [true, false, false, false, false, false]
  },
  "table": {
    data: ['lsmdb_table', 'tserver_table'],
    isOpen: [true, true]
  },
};

export default class CustomerMetricsPanel extends Component {
  static propTypes = {
    origin: PropTypes.oneOf(['customer', 'universe', 'table']).isRequired,
    nodePrefixes: PropTypes.array,
    width: PropTypes.number,
    tableName: PropTypes.string
  };

  static defaultProps = {
    nodePrefixes:[],
    tableName: null,
    width: null
  };

  componentWillMount() {
    const { customer: { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "metrics.display")) browserHistory.push('/');
  }

  render() {
    const { origin, nodePrefixes, width, tableName, isKubernetesUniverse } = this.props;
    const location = browserHistory.getCurrentLocation();
    const currentQuery = location.query;
    const graphPanelContainers = graphPanelTypes[origin].data.map(function (type, idx) {
      // if we have subtab query param, then we would have that metric tab open by default
      const isOpen = currentQuery.subtab ? type === currentQuery.subtab : graphPanelTypes[origin].isOpen[idx];
      return (<GraphPanelContainer key={idx} isOpen={isOpen} type={type} width={width}
                  nodePrefixes={nodePrefixes} tableName={tableName} isKubernetesUniverse={isKubernetesUniverse} />);
    });

    return (
      <GraphPanelHeaderContainer origin={origin}>
        <PanelGroup id={origin+" metrics"}>
          {graphPanelContainers}
        </PanelGroup>
      </GraphPanelHeaderContainer>
    );
  }
}
