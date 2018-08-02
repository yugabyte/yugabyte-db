// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { getPromiseState } from 'utils/PromiseUtils';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { browserHistory } from 'react-router';
import { YBModal, YBCheckBox } from '../../common/forms/fields';
import { isEmptyObject } from 'utils/ObjectUtils';
import { getReadOnlyCluster } from "../../../utils/UniverseUtils";

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
    const { type, universe: {currentUniverse: {data}}} = this.props;
    this.props.onHide();
    if (type === "primary") {
      this.props.submitDeleteUniverse(data.universeUUID, this.state.isForceDelete);
    } else {  
      const cluster = getReadOnlyCluster(data.universeDetails.clusters);
      if (isEmptyObject(cluster)) return;
      this.props.submitDeleteReadReplica(cluster.uuid, data.universeUUID, this.state.isForceDelete);
    }
  };

  componentWillReceiveProps(nextProps) {
    if (getPromiseState(this.props.universe.deleteUniverse).isLoading() && getPromiseState(nextProps.universe.deleteUniverse).isSuccess()) {
      this.props.fetchUniverseMetadata();
      browserHistory.push('/universes');
    }
  }

  render() {
    const { visible, title, body, onHide, universe: {currentUniverse: { data: { name }}}} = this.props;
    return (
      <YBModal visible={ visible }
                onHide={ onHide } submitLabel={'Yes'} cancelLabel={'No'} showCancelButton={true} title={ title + name } onFormSubmit={ this.confirmDelete }
                footerAccessory={ <YBCheckBox label={"Ignore Errors and Force Delete"} className="footer-accessory" input={{ checked: this.state.isForceDelete, onChange: this.toggleForceDelete }} />} >
        { body }
      </YBModal>
    );
  }
}
