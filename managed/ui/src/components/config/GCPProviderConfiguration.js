// Copyright (c) YugaByte, Inc.

import React from 'react';
import {ListGroup, ListGroupItem, Row, Col} from 'react-bootstrap';
import {YBButton} from '../common/forms/fields';
var Dropzone = require('react-dropzone');
import ProviderConfiguration from './ProviderConfiguration';

export default class GCPProviderConfiguration extends ProviderConfiguration {

  render() {
    return (
      <div className="provider-config-container">
        <div>
          <h2>Google Cloud Platform</h2>
        </div>
        <ListGroup>
          <ListGroupItem>
            Configure Google Cloud Platform service access for YugaWare.
            See <span className="heading-text"><a href="https://cloud.google.com/docs/" target="_blank">GCP documentation</a></span>.
          </ListGroupItem>
          <ListGroupItem>
            Create a service account client for YugaWare, and download the private key to your local machine.
          </ListGroupItem>
          <ListGroupItem>
            Upload the private key file from step 2:&nbsp;
            <Dropzone onDrop={this.onDrop} className="btn btn-default">
              <div>Choose File</div>
            </Dropzone>
            &nbsp;
            <span>File Name</span>
          </ListGroupItem>
        </ListGroup>
        <Row>
          <Col lg={4} lgOffset={8}>
            <YBButton btnText={"Cancel"} btnClass={"btn btn-default cancel-btn"}/>
            <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn"}/>
          </Col>
        </Row>
      </div>
    )
  }
}
