// Copyright (c) YugaByte, Inc.

import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { Link } from 'react-router';

import { isAvailable } from '../../../utils/LayoutUtils';
import { isKubernetesUniverse } from '../../../utils/UniverseUtils';
import { YBCost } from '../../common/descriptors';
import { UniverseStatusContainer } from '..';
import { CellLocationPanel } from './CellLocationPanel';
import { CellResourcesPanel } from './CellResourcePanel';
import { timeFormatter } from '../../../utils/TableFormatters';

export const YBUniverseItem = (props) => {
  const {
    universe,
    customer: { currentCustomer }
  } = props;
  const isPricingKnown = universe.resources?.pricingKnown;

  return (
    <div>
      <Link to={`/universes/${universe.universeUUID}`}>
        <div className="universe-list-item-name-status universe-list-flex">
          <Row>
            <Col sm={6}>
              <div className="universe-name-cell">{universe.name}</div>
            </Col>
            <Col sm={6} className="universe-create-date-container">
              <div>Created:</div>
              {timeFormatter(universe.creationDate)}
            </Col>
          </Row>
          <div className="list-universe-status-container">
            <UniverseStatusContainer
              currentUniverse={universe}
              showLabelText={true}
              refreshUniverseData={props.fetchUniverseMetadata}
              showAlertsBadge={true}
            />
          </div>
        </div>
      </Link>

      <div className="universe-list-item-detail universe-list-flex">
        <Row>
          <Col sm={6}>
            <CellLocationPanel isKubernetesUniverse={isKubernetesUniverse(universe)} {...props} />
          </Col>
          <Col sm={6}>
            <CellResourcesPanel {...props} />
          </Col>
        </Row>
        {isAvailable(currentCustomer.data.features, 'costs.universe_list') && (
          <div className="cell-cost">
            <div className="cell-cost-value">
              <YBCost
                value={props.universe.pricePerHour}
                multiplier="month"
                isPricingKnown={isPricingKnown}
              />
            </div>
            /month
          </div>
        )}
      </div>
    </div>
  );
};
