// Copyright (c) YugaByte, Inc.

import React, {Component} from 'react';
import { ListGroup, ListGroupItem } from 'react-bootstrap';
import { YBButton } from '../../common/forms/fields';
import {withRouter} from 'react-router';

var Dropzone = require('react-dropzone');

class GCPProviderConfiguration extends Component {

  render() {
    return (
      <div className="provider-config-container">
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
        <div className="form-action-button-container">
          <YBButton btnText={"Save"} btnClass={"btn btn-default save-btn pull-right"}/>
        </div>
      </div>
    )
  }
}

export default withRouter(GCPProviderConfiguration);
