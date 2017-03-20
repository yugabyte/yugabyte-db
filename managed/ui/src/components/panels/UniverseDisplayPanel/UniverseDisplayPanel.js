// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import {YBLoadingIcon} from '../../common/indicators';
import { isValidObject } from 'utils/ObjectUtils';
import { YBCost, DescriptionItem } from 'components/common/descriptors';
import { UniverseFormContainer, UniverseStatusContainer } from 'components/universes';
import './UniverseDisplayPanel.scss';

class CreateUniverseButtonComponent extends Component {
  render() {
    return (
      <Col lg={2} className="create-universe-button" {...this.props}>
        <div className="btn-icon">
          <i className="fa fa-plus"/>
        </div>
        <div className="display-name text-center">
          Create Universe
        </div>
      </Col>
    )
  }
}

class UniverseDisplayItem extends Component {
  render() {
    const {universe} = this.props;
    var costPerMonth = <span/>;
    if (!isValidObject(universe)) {
      return <span/>;
    }
    var replicationFactor = <span>{`${universe.universeDetails.userIntent.replicationFactor}`}</span>
    var numNodes = <span>{universe.universeDetails.userIntent.numNodes}</span>
    if (isValidObject(universe.pricePerHour)) {
      costPerMonth = <YBCost value={universe.pricePerHour} multiplier={"month"}/>
    }
    return (
      <Col lg={2} className="universe-display-item-container">
        <div className="display-name">
          <Link to={"/universes/" + universe.universeUUID}>
            {universe.name}
          </Link>
          <div className="float-right">
            <UniverseStatusContainer currentUniverse={universe} />
          </div>
        </div>
        <div className="description-item-list">
          <DescriptionItem title="Replication Factor">
            <span>{replicationFactor}</span>
          </DescriptionItem>
          <DescriptionItem title="Nodes">
            <span>{numNodes}</span>
          </DescriptionItem>
          <DescriptionItem title="Monthly Cost">
            <span>{costPerMonth}</span>
          </DescriptionItem>
        </div>
        <Row>
          <Col lg={6}>
            Read
          </Col>
          <Col lg={6}>
            Write
          </Col>
        </Row>
      </Col>
    )
  }
}

export default class UniverseDisplayPanel extends Component {
  render() {
    var self = this;
    const { universe: {universeList, loading, showModal, visibleModal}} = this.props;
    if (loading) {
      return <YBLoadingIcon/>;
    }
    if (!isValidObject(universeList)) {
      return <span/>;
    }
    var universeDisplayList = universeList.map(function(universeItem, idx){
      return <UniverseDisplayItem  key={universeItem.name+idx} universe={universeItem}/>
    });
     var createUniverseButton = <CreateUniverseButtonComponent onClick={() => self.props.showUniverseModal()}/>;
    return (
      <div className="universe-display-panel-container">
        <h2>Universes</h2>

        {universeDisplayList}
        {createUniverseButton}
        <UniverseFormContainer type="Create"
                               visible={showModal===true && visibleModal==="universeModal"}
                               onHide={this.props.closeUniverseModal} title="Create Universe" />
      </div>
    )
  }
}
