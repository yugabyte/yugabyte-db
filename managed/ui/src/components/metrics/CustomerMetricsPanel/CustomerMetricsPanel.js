// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics';
import PropTypes from 'prop-types';
import { PanelGroup } from 'react-bootstrap';

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

  render() {
    const { origin, nodePrefixes, width, tableName, isKubernetesUniverse } = this.props;
    const graphPanelContainers = graphPanelTypes[origin].data.map(function (type, idx) {
      return (<GraphPanelContainer key={idx} isOpen={graphPanelTypes[origin].isOpen[idx]} type={type} width={width}
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
