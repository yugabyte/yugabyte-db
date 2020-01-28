// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { connect } from 'react-redux';

import { isNonEmptyObject } from 'utils/ObjectUtils';
import { YBModalForm } from 'components/common/forms';
import { YBButton } from '../../common/forms/fields';
import { getPromiseState } from 'utils/PromiseUtils';

import { setAlertsConfig, setAlertsConfigResponse,
         updateBackupState, updateBackupStateResponse,
         fetchUniverseInfo, fetchUniverseInfoResponse} from '../../../actions/universe';

class UniverseAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false
    };
  }
  openModal = () => {
    this.setState((prevState, props) => {
      return {
        showModal: true
      }
    });
  }
  closeModal = () => {
    this.setState((prevState, props) => {
      return {
        showModal: false
      }
    });
  }
  componentWillReceiveProps = (nextProps) => {
    if (getPromiseState(nextProps.backupState).isSuccess()) {
        this.closeModal();
        this.props.fetchCurrentUniverse(nextProps.universe.universeUUID);
    }
  }

  performAction = () => {
    const { universe } = this.props;
    const takeBackups = universe.universeConfig.takeBackups === "true";
    this.props.updateBackupState(universe.universeUUID, !takeBackups);
  }

  render() {
    const { visible, onHide, btnClass, actionType, universe :{ universeConfig} } = this.props;
    let btnLabel = null;
    let btnIcon = null;
    let modalTitle = null;

    switch(actionType) {
      case "toggle-backup":
        const takeBackups = universeConfig.takeBackups === "true";
        btnLabel =  takeBackups ? "Disable Backup" : "Enable Backup";
        btnIcon = takeBackups ? "fa fa-pause" : "fa fa-play";
        modalTitle =`${btnLabel} for: ${this.props.universe.name}?`;

      // TODO: add the toggle for health check snooze here.
    }
    return (
      <div>
        <YBButton btnText={btnLabel} btnIcon={btnIcon}
                btnClass={`btn ${btnClass}`} onClick={this.openModal} />
        <YBModalForm title={modalTitle}
                 visible={this.state.showModal} onHide={this.closeModal}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 submitLabel={"Yes"}
                 className="universe-action-modal"
                 onFormSubmit={this.performAction}>
          <Row>
            <Col lg={12}>
            Are you sure you want to perform this action?
            </Col>
          </Row>
        </YBModalForm>
      </div>
    );
  }
}

const mapDispatchToProps = (dispatch) => {
  return {
    setAlertsConfig: (uuid, payload) => {
      dispatch(setAlertsConfig(uuid, payload))
        .then((response) => {
          dispatch(setAlertsConfigResponse(response.payload));
        });
    },
    updateBackupState: (uuid, flag) => {
      dispatch(updateBackupState(uuid, flag))
        .then((response) => {
          dispatch(updateBackupStateResponse(response.payload));
        });
    },

    fetchCurrentUniverse: (universeUUID) => {
      dispatch(fetchUniverseInfo(universeUUID)).then((response) => {
        dispatch(fetchUniverseInfoResponse(response.payload));
      });
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    alertsConfig: state.universe.alertsConfig,
    backupState: state.universe.backupState
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(UniverseAction);
