// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { YBPanelItem } from '../panels';
import { YBButton } from '../common/forms/fields';
import { UniverseFormContainer } from '../../containers/common/forms';
import { UniverseTableContainer } from '../../containers/universes';

export default class ListUniverse extends Component {

  render() {
    const {universe:{showModal, visibleModal}} = this.props;

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
            <UniverseFormContainer type="Create"
                                   visible={showModal===true && visibleModal==="universeModal"}
                                   onHide={this.props.closeUniverseModal} title="Create Universe" />
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
