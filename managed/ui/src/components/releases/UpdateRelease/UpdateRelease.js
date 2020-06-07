// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';

import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import { YBModalForm } from '../../../components/common/forms';

export default class UpdateRelease extends Component {
  updateRelease = () => {
    const {
      releaseInfo,
      updateYugaByteRelease,
      onModalSubmit,
      onHide,
      actionType
    } = this.props;
    updateYugaByteRelease(
      releaseInfo.version,
      {
        state: actionType
      }
    );
    onHide();
    onModalSubmit();
  };

  render() {
    const { visible, onHide, releaseInfo, actionType } = this.props;
    if (!isNonEmptyObject(releaseInfo)) {
      return <div />;
    }
    const modalTitle = "Switch Release " + releaseInfo.version + " to " + actionType + " state";
    return (
      <div className="universe-apps-modal">
        <YBModalForm title={modalTitle}
                 visible={visible} onHide={onHide}
                 showCancelButton={true}
                 cancelLabel={"Cancel"}
                 submitLabel={"Yes"}
                 className="import-release-modal"
                 onFormSubmit={this.updateRelease}>
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
