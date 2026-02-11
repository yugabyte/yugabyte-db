// Copyright (c) YugabyteDB, Inc.
import { Component } from 'react';
import { browserHistory } from 'react-router';
import { toast } from 'react-toastify';
import PropTypes from 'prop-types';
import { YBButton } from '@app/redesign/components';
import { YBModal } from '../../common/forms/fields';
import { NodeAction } from '../../universes';
import { createErrorMessage, isNonEmptyString } from '../../../utils/ObjectUtils';
import { getNodeActionStatusMsg } from '@app/utils/UniverseUtils';

const nodeActionExpectedResult = {
  START: 'Live',
  STOP: 'Stopped',
  REMOVE: 'Removed',
  RELEASE: 'Unreachable',
  DELETE: 'Unreachable'
};

export default class NodeActionModal extends Component {
  static propTypes = {
    nodeInfo: PropTypes.object.isRequired,
    actionType: PropTypes.string
  };

  pollNodeStatusUpdate = (universeUUID, actionType, nodeName, payload) => {
    const { getNodeDetails, getNodeDetailsResponse } = this.props;
    this.interval = setTimeout(() => {
      getNodeDetails(universeUUID, nodeName).then((response) => {
        if (response.payload?.data) {
          const node = response.payload.data;
          if (actionType === 'DELETE' || node.state === nodeActionExpectedResult[actionType]) {
            clearInterval(this.interval);
            getNodeDetailsResponse(response.payload);
            return;
          }
          getNodeDetailsResponse(response.payload);
          this.pollNodeStatusUpdate(universeUUID, actionType, nodeName, payload);
        }
      });
    }, 1500);
  };

  componentWillUnmount() {
    if (this.interval) {
      clearInterval(this.interval);
    }
  }

  performNodeAction = (runOnlyPrechecks = false) => {
    const {
      universe: { currentUniverse },
      nodeInfo,
      actionType,
      performUniverseNodeAction,
      onHide
    } = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    performUniverseNodeAction(universeUUID, nodeInfo.name, actionType, runOnlyPrechecks).then(
      (response) => {
        if (response.error !== true) {
          this.pollNodeStatusUpdate(universeUUID, actionType, nodeInfo.name, response.payload);
        } else if (response.error && response.payload.status !== 200) {
          toast.error(createErrorMessage(response.payload));
        }
      }
    );
    onHide();
    if (runOnlyPrechecks) {
      // Redirect to tasks page to view precheck results before performing further node actions
      browserHistory.push('/universes/' + universeUUID + '/tasks');
    } else {
      browserHistory.push('/universes/' + universeUUID + '/nodes');
    }
  };

  render() {
    const { visible, onHide, nodeInfo, actionType, precheckNodeActions } = this.props;
    console.warn('Nide Actions', precheckNodeActions);
    if (actionType === null || nodeInfo === null) {
      return <span />;
    }

    const modalMessage = getNodeActionStatusMsg(actionType);

    return (
      <div className="universe-apps-modal">
        <YBModal
          title={`Perform Node Action: ${NodeAction.getCaption(actionType)} `}
          visible={visible}
          onHide={onHide}
          showCancelButton={true}
          cancelLabel={'Cancel'}
          onFormSubmit={this.performNodeAction}
          footerAccessory={
            precheckNodeActions.includes(actionType) && (
              <div>
                <YBButton
                  onClick={() => this.performNodeAction(true)}
                  variant="primary"
                  data-testid="FullMoveModal-RunPrechecksButton"
                  size="large"
                >
                  {'Run Pre-check Only'}
                </YBButton>
              </div>
            )
          }
        >
          {`Are you sure you want to ${actionType.toLowerCase()}`}
          <b>{` ${nodeInfo.name}?`}</b>
          <br />
          {modalMessage && modalMessage?.purpose && (
            <>
              <br />
              <span style={{ whiteSpace: 'pre-line' }}>
                <b>{modalMessage.purpose}</b>
                <br />
                {isNonEmptyString(modalMessage?.description) && `\n${modalMessage.description}`}
              </span>
            </>
          )}
        </YBModal>
      </div>
    );
  }
}
