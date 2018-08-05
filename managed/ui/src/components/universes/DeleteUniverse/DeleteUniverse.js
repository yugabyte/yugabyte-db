// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Modal, Checkbox } from 'react-bootstrap';
import { getPromiseState } from 'utils/PromiseUtils';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { browserHistory } from 'react-router';
import { YBButton } from '../../common/forms/fields';
export default class DeleteUniverse extends Component {
  constructor(props) {
    super(props);
    this.state = {isForceDelete: false};
  }

  toggleForceDelete = () => {
    this.setState({isForceDelete: !this.state.isForceDelete});
  };

  closeDeleteModal = () => {
    this.props.onHide();
  };

  confirmDelete = () => {
    this.props.onHide();
    this.props.deleteUniverse(this.props.universe.currentUniverse.data.universeUUID, this.state.isForceDelete);
  };

  componentWillReceiveProps(nextProps) {
    if (getPromiseState(this.props.universe.deleteUniverse).isLoading() && getPromiseState(nextProps.universe.deleteUniverse).isSuccess()) {
      this.props.fetchUniverseMetadata();
      browserHistory.push('/universes');
    }
  }

  render() {
    const { visible, onHide, universe: {currentUniverse: {data: {name}}}} = this.props;
    return (
      <Modal show={visible} onHide={onHide}>
        <Modal.Header>
          <Modal.Title>
            Delete Universe: { name }
          </Modal.Title>
        </Modal.Header>
        <Modal.Body>
          Are you sure you want to delete the universe. You will lose all your data!
        </Modal.Body>
        <Modal.Footer>
          <Checkbox inline className="delete-universe-check-container" checked={this.state.isForceDelete} onChange={this.toggleForceDelete}>
            &nbsp;Ignore Errors and Force Delete
          </Checkbox>
          <YBButton onClick={this.closeDeleteModal} btnText="No"/>
          <YBButton btnStyle="primary" onClick={this.confirmDelete} btnText="Yes"/>
        </Modal.Footer>
      </Modal>
    );
  }
}
