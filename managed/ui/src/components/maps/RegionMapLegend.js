// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {ListGroup, ListGroupItem, Checkbox} from 'react-bootstrap';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import './stylesheets/RegionMapLegend.css'

export default class RegionMapLegend extends Component{
  render() {
    const {providers} = this.props;
    var providerList = <ListGroupItem>
                         No Providers Configured
                       </ListGroupItem>;
    if (isNonEmptyArray(providers)) {
      providerList = providers.map(function (item, idx) {
        return  (
          <ListGroupItem key={idx}>
            <Checkbox inline/>
            {item.name}
          </ListGroupItem>
        )
      })
    } 
    return (
      <div className="region-map-legend-container">
        <h4>Cloud Providers</h4>
        <span>Select <small>All |  None</small></span>
        <ListGroup>
          {providerList}
        </ListGroup>
      </div>
    )
  }
}
