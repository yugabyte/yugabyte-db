// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import { isFinite } from 'lodash';
import { YBLoading } from '../../common/indicators';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBCost, DescriptionItem } from '../../../components/common/descriptors';
import { UniverseStatusContainer } from '../../../components/universes';
import './UniverseDisplayPanel.scss';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBButton } from '../../common/forms/fields';
import {
  getPrimaryCluster,
  getClusterProviderUUIDs,
  getProviderMetadata,
  getUniverseNodeCount
} from '../../../utils/UniverseUtils';
import { isNotHidden, isDisabled } from '../../../utils/LayoutUtils';
import { TimestampWithTimezone } from '../../common/timestampWithTimezone/TimestampWithTimezone';

class CTAButton extends Component {
  render() {
    const { linkTo, labelText, otherProps } = this.props;

    return (
      <Link to={linkTo}>
        <div className="create-universe-button" {...otherProps}>
          <div className="btn-icon">
            <i className="fa fa-plus" />
          </div>
          <div className="display-name text-center">{labelText}</div>
        </div>
      </Link>
    );
  }
}

class UniverseDisplayItem extends Component {
  render() {
    const { universe, providers, refreshUniverseData } = this.props;
    if (!isNonEmptyObject(universe)) {
      return <span />;
    }
    const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster) || !isNonEmptyObject(primaryCluster.userIntent)) {
      return <span />;
    }
    const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
    const clusterProviders = providers.data.filter((p) => clusterProviderUUIDs.includes(p.uuid));
    const replicationFactor = <span>{`${primaryCluster.userIntent.replicationFactor}`}</span>;
    const universeProviders = clusterProviders.map((provider) => {
      return getProviderMetadata(provider).name;
    });
    const universeProviderText = universeProviders.join(', ');

    const nodeCount = getUniverseNodeCount(universe.universeDetails.nodeDetailsSet);
    const isPricingKnown = universe.resources?.pricingKnown;
    const pricePerHour = universe.pricePerHour;
    const numNodes = <span>{nodeCount}</span>;
    let costPerMonth = <span>n/a</span>;
    if (isFinite(pricePerHour)) {
      costPerMonth = <YBCost
        value={pricePerHour}
        multiplier={'month'}
        isPricingKnown={isPricingKnown}
      />;
    }
    const universeCreationDate = universe.creationDate ? (
      <TimestampWithTimezone timeFormat="MM/DD/YYYY" timestamp={universe.creationDate} />
    ) : (
      ''
    );
    return (
      <Col sm={4} md={3} lg={2}>
        <Link to={'/universes/' + universe.universeUUID}>
          <div className="universe-display-item-container">
            <div className="status-icon">
              <UniverseStatusContainer
                currentUniverse={universe}
                refreshUniverseData={refreshUniverseData}
              />
            </div>
            <div className="display-name">{universe.name}</div>
            <div className="provider-name">{universeProviderText}</div>
            <div className="description-item-list">
              <DescriptionItem title="Nodes">
                <span>{numNodes}</span>
              </DescriptionItem>
              <DescriptionItem title="Replication Factor">
                <span>{replicationFactor}</span>
              </DescriptionItem>
              <DescriptionItem title="Monthly Cost">
                <span>{costPerMonth}</span>
              </DescriptionItem>
              <DescriptionItem title="Created">
                <span>{universeCreationDate}</span>
              </DescriptionItem>
            </div>
          </div>
        </Link>
      </Col>
    );
  }
}

export default class UniverseDisplayPanel extends Component {
  render() {
    const self = this;
    const {
      universe: { universeList },
      cloud: { providers },
      customer: { currentCustomer }
    } = this.props;
    if (getPromiseState(providers).isSuccess()) {
      let universeDisplayList = <span />;
      if (getPromiseState(universeList).isSuccess()) {
        universeDisplayList = universeList.data
          .sort((a, b) => {
            return Date.parse(a.creationDate) < Date.parse(b.creationDate);
          })
          .map(function (universeItem, idx) {
            return (
              <UniverseDisplayItem
                key={universeItem.name + idx}
                universe={universeItem}
                providers={providers}
                refreshUniverseData={self.props.fetchUniverseMetadata}
              />
            );
          });
      }

      return (
        <div className="universe-display-panel-container">
          <Row xs={6}>
            <Col xs={3}>
              <h2>Universes</h2>
            </Col>
            <Col className="universe-table-header-action dashboard-universe-actions">
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
            </Col>
          </Row>
          <Row className="list-group">{universeDisplayList}</Row>
        </div>
      );
    } else if (getPromiseState(providers).isEmpty()) {
      return (
        <div className="get-started-config">
          <span className="yb-data-name">
            Welcome to the <div>YugaByte Admin Console.</div>
          </span>
          <span>Before you can create a Universe, you must configure a cloud provider.</span>
          <CTAButton linkTo={'config'} labelText={'Configure a Provider'} />
        </div>
      );
    } else {
      return <YBLoading />;
    }
  }
}
