// Copyright (c) YugaByte, Inc.

import { Component } from 'react';
import { getPromiseState } from '../../../utils/PromiseUtils';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { browserHistory } from 'react-router';
import { Alert } from 'react-bootstrap';
import { YBModal, YBCheckBox, YBTextInput } from '../../common/forms/fields';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import { getReadOnlyCluster } from '../../../utils/UniverseUtils';
import { RbacValidator, hasNecessaryPerm } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';

export default class DeleteUniverse extends Component {
  constructor(props) {
    super(props);
    this.state = {
      isForceDelete: false,
      isDeleteBackups: false,
      universeName: false
    };
  }

  toggleForceDelete = () => {
    this.setState({ isForceDelete: !this.state.isForceDelete });
  };

  toggleDeleteBackups = () => {
    this.setState({ isDeleteBackups: !this.state.isDeleteBackups });
  };

  onChangeUniverseName = (value) => {
    this.setState({ universeName: value });
  };

  closeDeleteModal = () => {
    this.props.onHide();
  };

  getModalBody = () => {
    const {
      body,
      universe: {
        currentUniverse: { data }
      },
      focusedUniverse = null
    } = this.props;
    const { name, universeDetails } = focusedUniverse ? focusedUniverse : data;

    const universePaused = universeDetails?.universePaused;

    return (
      <>
        {universePaused ? (
          <>
            Are you sure you want to delete the universe?
            <Alert bsStyle="danger">
              <strong>Note: </strong>
              {
                "Terminating paused universes won't delete backup objects. If \
              you want to delete backup objects, resume this universe and then delete it."
              }
            </Alert>
          </>
        ) : (
          <>
            {body}
            <br />
          </>
        )}
        <br />
        <label>Enter universe name to confirm delete:</label>
        <YBTextInput
          label="Confirm universe name:"
          placeHolder={name}
          input={{ onChange: this.onChangeUniverseName, onBlur: () => { } }}
        />
      </>
    );
  };

  confirmDelete = () => {
    const {
      type,
      universe: {
        currentUniverse: { data }
      },
      focusedUniverse,
      submitDeleteUniverse,
      submitDeleteReadReplica
    } = this.props;
    const { universeUUID, universeDetails } = focusedUniverse ? focusedUniverse : data;

    this.props.onHide();
    if (type === 'primary') {
      submitDeleteUniverse(universeUUID, this.state.isForceDelete, this.state.isDeleteBackups);
    } else {
      const cluster = getReadOnlyCluster(universeDetails.clusters);
      if (isEmptyObject(cluster)) return;
      submitDeleteReadReplica(cluster.uuid, universeUUID, this.state.isForceDelete);
    }
  };

  componentDidUpdate(prevProps) {
    if (
      getPromiseState(prevProps.universe.deleteUniverse).isLoading() &&
      getPromiseState(this.props.universe.deleteUniverse).isSuccess()
    ) {
      this.props.fetchUniverseMetadata();
      if (this.props.location.pathname !== '/universes') {
        browserHistory.push('/universes');
      }
    }
  }

  render() {
    const {
      visible,
      title,
      error,
      onHide,
      universe: {
        currentUniverse: { data }
      },
      focusedUniverse
    } = this.props;
    const { name, universeDetails } = focusedUniverse ? focusedUniverse : data;

    const universePaused = universeDetails?.universePaused;

    return (
      <YBModal
        visible={visible}
        formName={'DeleteUniverseForm'}
        onHide={onHide}
        submitLabel={'Yes'}
        cancelLabel={'No'}
        showCancelButton={true}
        title={title + name}
        onFormSubmit={this.confirmDelete}
        error={error}
        footerAccessory={
          <div className="force-delete">
            <YBCheckBox
              label="Ignore Errors and Force Delete"
              className="footer-accessory"
              input={{ checked: this.state.isForceDelete, onChange: this.toggleForceDelete }}
            />
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.DELETE_BACKUP}
              isControl
              popOverOverrides={{ zIndex: 10000 }}
            >
              <YBCheckBox
                label="Delete Backups"
                className="footer-accessory"
                disabled={universePaused || !hasNecessaryPerm(ApiPermissionMap.DELETE_BACKUP)}
                input={{ checked: this.state.isDeleteBackups, onChange: this.toggleDeleteBackups }}
              />
            </RbacValidator>
          </div>
        }
        asyncValidating={this.state.universeName !== name}
      >
        {this.getModalBody()}
      </YBModal>
    );
  }
}
