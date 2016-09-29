// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import UniverseTableContainer from '../containers/UniverseTableContainer';
import YBPanelItem from './YBPanelItem';
import { Row, Col } from 'react-bootstrap';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import YBButton from './fields/YBButton';

export default class ListUniverse extends Component {
  
  render() {
    const {universe:{showModal}} = this.props;
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={10} className="universe-table-header">
            <h3>Universes<small> Status and details</small></h3>
          </Col>
          <Col lg={1} className="universe-table-header-action">
            <YBButton btnClass="universe-button btn btn-default btn-lg bg-orange"
                           btnText="Create Universe" btnIcon="fa fa-pencil"
                           onClick={this.props.showUniverseModal} />
            <UniverseModalContainer type="Create"
                                    visible={showModal}
                                    onClose={this.props.closeUniverseModal} />
          </Col>
        </Row>
        <div>
          <YBPanelItem name="Universe List">
            <UniverseTableContainer />
          </YBPanelItem>
        </div>
      </div>
    )
  }
}
