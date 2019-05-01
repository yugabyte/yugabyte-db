// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { UniverseRegionLocationPanelContainer, HighlightedStatsPanelContainer,
  UniverseDisplayPanelContainer } from '../panels';
import './stylesheets/Dashboard.scss';
import { browserHistory} from 'react-router';
import { isAvailable, isNonAvailable } from 'utils/LayoutUtils';

export default class Dashboard extends Component {

  componentWillMount() {
    const { customer : { currentCustomer }} = this.props;
    if (isNonAvailable(currentCustomer.data.features, "dashboard.display")) browserHistory.push('/');
  }

  render() {
    const { customer: { currentCustomer } } = this.props;
    return (
      <div id="page-wrapper">
        {isAvailable(currentCustomer.data.features, "main.stats") && <div className="dashboard-stats">
          <HighlightedStatsPanelContainer />
        </div>}
        <UniverseDisplayPanelContainer {...this.props}/>
        <Row>
          <Col lg={12}>
            <UniverseRegionLocationPanelContainer {...this.props}/>
          </Col>
        </Row>
      </div>
    );
  }
  }
