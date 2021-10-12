// Copyright (c) YugaByte, Inc.

import React, { useEffect } from 'react';
import { Row, Col } from 'react-bootstrap';
import { Link } from 'react-router';

import { YBButton } from '../../common/forms/fields';
import { UniverseViewContainer } from '..';
import { HighlightedStatsPanelContainer } from '../../panels';
import { isNotHidden, isDisabled, isAvailable, showOrRedirect } from '../../../utils/LayoutUtils';

import './UniverseConsole.scss';

export const UniverseConsole = (props) => {
  const {
    customer: { currentCustomer },
    fetchUniverseList
  } = props;

  useEffect(() => {
    fetchUniverseList();
  }, [fetchUniverseList]);

  showOrRedirect(currentCustomer.data.features, 'main.universe_list');

  return (
    <div id="page-wrapper">
      <Row className="header-row">
        <Col xs={6}>
          <h2 className="content-title">Universes</h2>
          {isAvailable(currentCustomer.data.features, 'main.stats') && (
            <div className="dashboard-stats">
              <HighlightedStatsPanelContainer />
            </div>
          )}
        </Col>
      </Row>
      <UniverseViewContainer>
        {isNotHidden(currentCustomer.data.features, 'universe.import') && (
          <Link to="/universes/import">
            <YBButton
              btnClass="universe-button btn btn-lg btn-default"
              disabled={isDisabled(currentCustomer.data.features, 'universe.import')}
              btnText="Import Universe"
              btnIcon="fa fa-mail-forward"
            />
          </Link>
        )}
        {isNotHidden(currentCustomer.data.features, 'universe.create') && (
          <Link to="/universes/create">
            <YBButton
              btnClass="universe-button btn btn-lg btn-orange"
              disabled={isDisabled(currentCustomer.data.features, 'universe.create')}
              btnText="Create Universe"
              btnIcon="fa fa-plus"
            />
          </Link>
        )}
      </UniverseViewContainer>
    </div>
  );
};
