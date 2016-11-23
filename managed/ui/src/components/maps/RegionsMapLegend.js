// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {ListGroup, ListGroupItem, Checkbox} from 'react-bootstrap';


export default class SupportedRegionsMapLegend extends Component{
  render() {
    return (
      <div className="supported-map-legend-container">
        <h3>Platforms</h3>
        <span>Select <small>All |  None</small></span>
        <ListGroup>
          <ListGroupItem>Google Cloud Platform <Checkbox inline/></ListGroupItem>
          <ListGroupItem>Amazon Web Services <Checkbox inline/></ListGroupItem>
          <ListGroupItem>Docker Localhost <Checkbox inline/></ListGroupItem>
        </ListGroup>
      </div>
    )
  }
}
