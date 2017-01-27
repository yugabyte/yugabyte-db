// Copyright (c) YugaByte, Inc.

import React from 'react';
import {ListGroup, ListGroupItem, Well, Col, Row} from 'react-bootstrap';
import {YBButton} from '../../common/forms/fields';
import ProviderConfiguration from '../ConfigProvider/ProviderConfiguration';

export default class DockerProviderConfiguration extends ProviderConfiguration {
  render() {
    return (
      <div className="provider-config-container">
         <h2>Docker Configuration</h2>
        <ListGroup>
          <ListGroupItem>
            Ensure that
            <span className="heading-text"><a href="https://docs.docker.com/" target="_blank"> Docker Platform </a></span>
            is running on your local machine.
          </ListGroupItem>
          <ListGroupItem>
            Create a universe with three nodes, each in its own container.
            <YBButton btnText="Create Initial Docker Universe" btnClass="btn btn-default bg-orange"/>
          </ListGroupItem>
          <ListGroupItem>
            Verify universe creation by checking for the following logs:
            <Well>
              2016-12-09 14:27:49, 410 INFO: Running ansible command "ansible-playbook"
              "/Users/shawn/code/devops/configure-cluster-server.yml" "vault-password-file"
              "/Users/shawn/.yugabyte/vault_password" "-i" Lorem ipsum dolor sit amet, consectetur
              adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim
              ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo
              consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu
              fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui
              officia deserunt mollit anim id est laborum Lorem ipsum dolor sit amet, consectetur
              adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.
              2016-12-09 14:28:23, 410 Universe fake-universe ready.
            </Well>
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
