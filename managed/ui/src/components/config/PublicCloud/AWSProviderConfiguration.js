// Copyright (c) YugaByte, Inc.

import React from 'react';
import {ListGroup, ListGroupItem, Row, Col} from 'react-bootstrap';
import {YBButton} from '../../common/forms/fields'
import ProviderConfiguration from '../ConfigProvider/ProviderConfiguration';

export default class AWSProviderConfiguration extends ProviderConfiguration {
  render() {
    return (
      <div className="provider-config-container">
        <h2>Amazon Web Services</h2>
        <ListGroup>
          <ListGroupItem>
            Configure AWS third-party account access for YugaWare.
            See <span className="heading-text"><a href="https://aws.amazon.com/documentation/" target="_blank">AWS documentation</a></span>.
          </ListGroupItem>
          <ListGroupItem>
            Create a new IAM Policy in AWS IAM using
            <span className="heading-text"><a href="https://aws.amazon.com/documentation/" target="_blank"> this sample policy.</a></span>
            <div className="detail-text">
              <span>
                You will need the following details.
              </span>
              <span>
                If using YugaWare Cloud:
                <span>YugaWare Cloud AWS Account ID: 123456789012</span>
                <span>External ID: yugabyte-mycompany-1</span>
              </span>
              <span>
                If using YugaWare On-Premise:
                <span>AWS Account ID: your-aws-account-id-where-yugaware-is-deployed</span>
              </span>
            </div>
          </ListGroupItem>
          <ListGroupItem>
            Enter the Role ARN generated in step 2:
            <input type="text"/>
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
