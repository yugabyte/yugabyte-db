// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { find } from 'lodash';
import {
  UniverseRegionLocationPanelContainer,
  HighlightedStatsPanelContainer,
  UniverseDisplayPanelContainer
} from '../panels';
import './stylesheets/Dashboard.scss';
import { isAvailable, showOrRedirect } from '../../utils/LayoutUtils';
import { customPermValidateFunction } from '../../redesign/features/rbac/common/RbacApiPermValidator';
import { getWrappedChildren } from '../../redesign/features/rbac/common/validator/ValidatorUtils';
import { Action, Resource } from '../../redesign/features/rbac';
import { userhavePermInRoleBindings } from '../../redesign/features/rbac/common/RbacUtils';

export default class Dashboard extends Component {
  componentDidMount() {
    this.props.fetchUniverseList();
  }

  render() {
    const {
      customer: { currentCustomer }
    } = this.props;
    showOrRedirect(currentCustomer.data.features, 'menu.dashboard');

    if (!customPermValidateFunction(() => {

      return userhavePermInRoleBindings(Resource.UNIVERSE, Action.READ);
    })) {
      return getWrappedChildren({});
    }

    return (
      <div id="page-wrapper">
        {isAvailable(currentCustomer.data.features, 'main.stats') && (
          <div className="dashboard-stats">
            <HighlightedStatsPanelContainer />
          </div>
        )}
        <UniverseDisplayPanelContainer {...this.props} />
        <Row>
          <Col lg={12}>
            <UniverseRegionLocationPanelContainer {...this.props} />
          </Col>
        </Row>
      </div>
    );
  }
}
