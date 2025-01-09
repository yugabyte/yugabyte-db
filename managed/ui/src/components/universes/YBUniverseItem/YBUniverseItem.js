// Copyright (c) YugaByte, Inc.

import { Col, Row } from 'react-bootstrap';
import { Link } from 'react-router';

import { isAvailable } from '../../../utils/LayoutUtils';
import {
  getIsKubernetesUniverse,
  getPrimaryCluster,
  optimizeVersion
} from '../../../utils/UniverseUtils';
import { YBCost } from '../../common/descriptors';
import { UniverseStatusContainer } from '..';
import { CellLocationPanel } from './CellLocationPanel';
import { CellResourcesPanel } from './CellResourcePanel';
import { timeFormatter } from '../../../utils/TableFormatters';
import { useIsTaskNewUIEnabled } from '../../../redesign/features/tasks/TaskUtils';

export const YBUniverseItem = (props) => {
  const {
    universe,
    runtimeConfigs,
    customer: { currentCustomer }
  } = props;
  const isPricingKnown = universe.resources?.pricingKnown;
  const primaryCluster = getPrimaryCluster(universe?.universeDetails?.clusters);
  const isNewTaskDetailsUIEnabled = useIsTaskNewUIEnabled();
  return (
    <div>
      <Link to={`/universes/${universe.universeUUID}`}>
        <div
          className={`universe-list-item-name-status universe-list-flex ${
            isNewTaskDetailsUIEnabled && 'new-task-details'
          }`}
        >
          <Row>
            <Col sm={6}>
              <div className="universe-name-cell">{universe.name}</div>
            </Col>
            <Col sm={6} className="inline-flex">
              <div className="universe-create-version-container mr-5">
                <div>Version:</div>
                {optimizeVersion(
                  primaryCluster?.userIntent?.ybSoftwareVersion.split('-')[0].split('.')
                )}
              </div>
              <div className="universe-create-date-container">
                <div>Created:</div>
                {timeFormatter(universe.creationDate)}
              </div>
            </Col>
          </Row>
          <div className="list-universe-status-container">
            <UniverseStatusContainer
              currentUniverse={universe}
              showLabelText={true}
              refreshUniverseData={props.fetchUniverseMetadata}
              shouldDisplayTaskButton={false}
              showAlertsBadge={true}
              showTaskDetails={true}
            />
          </div>
        </div>
      </Link>

      <div className="universe-list-item-detail universe-list-flex">
        <Row>
          <Col sm={6}>
            <CellLocationPanel
              isKubernetesUniverse={getIsKubernetesUniverse(universe)}
              {...props}
            />
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
                runtimeConfigs={runtimeConfigs}
              />
            </div>
            /month
          </div>
        )}
      </div>
    </div>
  );
};
