// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Link} from 'react-router';
import { Row, Col, Checkbox } from 'react-bootstrap';

import { isValidObject } from '../../../utils/ObjectUtils';
import { YBCost, DescriptionItem } from '../../common/descriptors';
import { UniverseFormContainer } from '../../common/forms';
import './UniverseDisplayPanel.css'

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
    const {universeDetailItem} = this.props;
    var costPerMonth = <span/>;
    if (!isValidObject(universeDetailItem)) {
      return <span/>;
    }
    var replicationFactor = <span>{`${universeDetailItem.universeDetails.userIntent.replicationFactor}x`}</span>
    var numNodes = <span>{universeDetailItem.universeDetails.userIntent.replicationFactor}</span>
    if (isValidObject(universeDetailItem.pricePerHour)) {
      costPerMonth = <YBCost value={universeDetailItem.pricePerHour} multiplier={"month"}/>
    }
    return (
      <Col lg={2} className="universe-display-item-container">
        <div className="display-name">
          <Link to={"/universes/" + universeDetailItem.universeUUID}>
            {universeDetailItem.name}
          </Link>
          <div className="float-right"><Checkbox inline/></div>
        </div>
        <DescriptionItem title="Replication Factor">
          <span>{replicationFactor}</span>
        </DescriptionItem>
        <DescriptionItem title="Number of Nodes">
          <span>{numNodes}</span>
        </DescriptionItem>
        <DescriptionItem title="Cost">
          <span>{costPerMonth}</span>
        </DescriptionItem>
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
      return <div className="container">Loading...</div>;
    }
    if (!isValidObject(universeList)) {
      return <span/>;
    }
    var universeDisplayList = universeList.map(function(universeItem, idx){
        return <UniverseDisplayItem  key={universeItem.name+idx} universeDetailItem={universeItem}/>
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
