// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {ListGroup, ListGroupItem, Checkbox} from 'react-bootstrap';

import './stylesheets/RegionMapLegend.css'

export default class RegionMapLegend extends Component{
  render() {
    return (
      <div className="region-map-legend-container">
        <h4>Cloud Providers</h4>
        <span>Select <small>All |  None</small></span>
        <ListGroup>
          <ListGroupItem>
            <Checkbox inline/>
            Google Cloud Platform
          </ListGroupItem>
          <ListGroupItem>
            <Checkbox inline/>
            Amazon Web Services
          </ListGroupItem>
          <ListGroupItem>
            <Checkbox inline/>
            Docker Localhost
          </ListGroupItem>
        </ListGroup>
      </div>
    )
  }
}
