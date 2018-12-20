// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { browserHistory, Link } from 'react-router';
import { YBButton } from 'components/common/forms/fields';
import { UniverseTableContainer } from 'components/universes';
import { HighlightedStatsPanelContainer } from '../../panels';
import './ListUniverse.scss';

export default class ListUniverse extends Component {
  createNewUniverse = () => {
    browserHistory.push("/universes/create");
  };

  render() {
    return (
      <div id="page-wrapper">
        <Row className="header-row">
          <Col xs={6}>
            <h2 className="content-title">Universes</h2>
            <HighlightedStatsPanelContainer />
          </Col>
          <Col xs={6} className="universe-table-header-action">

            <Link to="/importer"><YBButton btnClass="universe-button btn btn-lg btn-default"
                           btnText="Import Universe" btnIcon="fa fa-mail-forward"/></Link>
            <YBButton btnClass="universe-button btn btn-lg btn-orange"
                           btnText="Create Universe" btnIcon="fa fa-pencil"
                           onClick={this.createNewUniverse} />
          </Col>
        </Row>
        <UniverseTableContainer />
      </div>
    );
  }
}
