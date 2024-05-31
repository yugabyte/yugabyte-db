// Copyright (c) YugaByte, Inc.

import { useEffect } from 'react';
import { Row, Col } from 'react-bootstrap';

import { UniverseViewContainer } from '..';
import { showOrRedirect } from '../../../utils/LayoutUtils';

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
        </Col>
      </Row>
      <UniverseViewContainer />
    </div>
  );
};
