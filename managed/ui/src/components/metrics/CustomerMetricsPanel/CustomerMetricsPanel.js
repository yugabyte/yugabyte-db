// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { GraphPanelHeaderContainer, GraphPanelContainer } from '../../metrics';
import PropTypes from 'prop-types';
import { PanelGroup } from 'react-bootstrap';

const graphPanelTypes = {
  "universe": ['proxies', 'server', 'cql', 'redis', 'tserver', 'lsmdb'],
  "customer": ['proxies', 'server', 'cql', 'redis', 'tserver', 'lsmdb'],
  "table": ['lsmdb_table', 'tserver_table']
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
    const { origin, nodePrefixes, width, tableName } = this.props;
    const graphPanelContainers = graphPanelTypes[origin].map(function (type, idx) {
      return (<GraphPanelContainer key={idx} isOpen={idx?false:true} type={type} width={width}
                  nodePrefixes={nodePrefixes} tableName={tableName} />);
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
