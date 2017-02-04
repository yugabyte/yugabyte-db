// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { YBButton } from 'components/common/forms/fields';
import { UniverseFormContainer, UniverseTableContainer } from 'components/universes';
import './ListUniverse.css';

export default class ListUniverse extends Component {

  render() {
    const {universe:{showModal, visibleModal}} = this.props;

    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col lg={10} className="universe-table-header">
            <h2>Universes</h2>
          </Col>
          <Col lg={1} className="universe-table-header-action">
            <YBButton btnClass="universe-button btn btn-default btn-lg bg-orange"
                           btnText="Create Universe" btnIcon="fa fa-pencil"
                           onClick={this.props.showUniverseModal} />
            <UniverseFormContainer type="Create"
                                   visible={showModal === true && visibleModal === "universeModal"}
                                   onHide={this.props.closeUniverseModal} title="Create Universe" />
          </Col>
        </Row>
        <Row>
          <Col lg={12}>
            <UniverseTableContainer />
          </Col>
        </Row>
      </div>
    )
  }
}
