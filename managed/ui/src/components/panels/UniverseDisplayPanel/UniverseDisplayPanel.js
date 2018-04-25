// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import { isFinite } from 'lodash';
import { YBLoading } from '../../common/indicators';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBCost, DescriptionItem } from 'components/common/descriptors';
import { UniverseStatusContainer } from 'components/universes';
import './UniverseDisplayPanel.scss';
import { isNonEmptyObject } from "../../../utils/ObjectUtils";
import { getPrimaryCluster, getReadOnlyCluster, getClusterProviderUUIDs, getProviderMetadata } from "../../../utils/UniverseUtils";
const moment = require('moment');

class CreateUniverseButtonComponent extends Component {
  render() {
    return (
      <Col sm={4} md={3} lg={2}>
        <Link to="/universes/create">
          <div className="create-universe-button" {...this.props}>
            <div className="btn-icon">
              <i className="fa fa-plus"/>
            </div>
            <div className="display-name text-center">
            Create Universe
          </div>
          </div>
        </Link>
      </Col>
    );
  }
}

class UniverseDisplayItem extends Component {
  render() {
    const {universe, providers, refreshUniverseData} = this.props;
    if (!isNonEmptyObject(universe)) {
      return <span/>;
    }
    const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster) || !isNonEmptyObject(primaryCluster.userIntent)) {
      return <span/>;
    }
    const readOnlyCluster = getReadOnlyCluster(universe.universeDetails.clusters);
    const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
    const clusterProviders = providers.data.filter((p) => clusterProviderUUIDs.includes(p.uuid));
    const replicationFactor = <span>{`${primaryCluster.userIntent.replicationFactor}`}</span>;
    const universeProviders = clusterProviders.map((provider) => {
      return getProviderMetadata(provider).name;
    });
    const universeProviderText = universeProviders.join(", ");
    let nodeCount = primaryCluster.userIntent.numNodes;
    if (isNonEmptyObject(readOnlyCluster)) {
      nodeCount += readOnlyCluster.userIntent.numNodes;
    }
    const numNodes = <span>{nodeCount}</span>;
    let costPerMonth = <span>n/a</span>;
    if (isFinite(universe.pricePerHour)) {
      costPerMonth = <YBCost value={universe.pricePerHour} multiplier={"month"}/>;
    }
    const universeCreationDate = universe.creationDate ? moment(Date.parse(universe.creationDate), "x").format("MM/DD/YYYY") : "";
    return (
      <Col sm={4} md={3} lg={2}>
        <Link to={"/universes/" + universe.universeUUID}>
          <div className="universe-display-item-container">
            <div className="status-icon">
              <UniverseStatusContainer currentUniverse={universe} refreshUniverseData={refreshUniverseData} />
            </div>
            <div className="display-name">
              {universe.name}
            </div>
            <div className="provider-name">
              {universeProviderText}
            </div>
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
    const { universe: {universeList}, cloud: {providers}} = this.props;
    if (getPromiseState(providers).isSuccess()) {
      let universeDisplayList = <span/>;
      if (getPromiseState(universeList).isSuccess()) {
        universeDisplayList = universeList.data.sort((a, b) => {
          return Date.parse(a.creationDate) < Date.parse(b.creationDate);
        }).map(function (universeItem, idx) {
          return (<UniverseDisplayItem key={universeItem.name + idx}
                                       universe={universeItem}
                                       providers={providers}
                                       refreshUniverseData={self.props.fetchUniverseMetadata} />);
        });
      }
      const createUniverseButton = <CreateUniverseButtonComponent onClick={() => self.props.showUniverseModal()}/>;
      return (
        <div className="universe-display-panel-container">
          <h2>Universes</h2>
          <Row>
            {universeDisplayList}
            {createUniverseButton}
          </Row>
        </div>
      );
    } else if (getPromiseState(providers).isEmpty()) {
      return (
        <div className="get-started-config">
          <span>Welcome to the <div className="yb-data-name">YugaByte Admin Console.</div></span>
          <span>Before you can create a Universe, you must configure a cloud provider.</span>
          <span><Link to="config">Click Here to Configure A Provider</Link></span>
        </div>
      );
    } else {
      return <YBLoading />;
    }
  }
}
