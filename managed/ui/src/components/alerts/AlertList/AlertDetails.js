import React, { Component } from 'react';
import { Row, Col, Label, ButtonGroup, DropdownButton, MenuItem } from 'react-bootstrap';
import './AlertDetails.scss';

const findValueforlabel = (labels, labelToFind) => {
  const label = labels.find((l) => l.name === labelToFind);
  return label ? label.value : '';
};

function getSeverityLabel(severity) {
  let labelType = '';
  switch (severity) {
    case 'SEVERE':
      labelType = 'danger';
      break;
    case 'RESOLVED':
      labelType = 'success';
      break;
    case 'WARNING':
    default:
      labelType = 'warning';
  }
  return <Label bsStyle={labelType}>{severity}</Label>;
}

export default class AlertDetails extends Component {
  shouldComponentUpdate(nextProps) {
    const { visible, alertDetails } = this.props;

    return (
      visible !== nextProps.visible ||
      nextProps.alertDetails !== alertDetails ||
      (alertDetails != null && nextProps.alertDetails.uuid !== alertDetails.uuid)
    );
  }

  render() {
    const { onHide, alertDetails, onAcknowledge } = this.props;

    if (!alertDetails) return null;

    const source_name = findValueforlabel(alertDetails.labels, 'source_name');
    return (
      <div id="universe-tab-panel-pane-queries" className={'alert-details'}>
        <div className={`side-panel`}>
          <div className="side-panel__header">
            <span className="side-panel__icon--close" onClick={onHide}>
              <i className="fa fa-close" />
            </span>
            <div className="side-panel__title">Alert Details</div>
          </div>
          <div className="side-panel__content">
            <Row>
              <Col lg={12} title={alertDetails.message} className="alert-message noLeftPadding">
                {alertDetails.message}
              </Col>
            </Row>
            <Row>
              <Col lg={12} className="noLeftPadding">
                {getSeverityLabel(alertDetails.severity)}
              </Col>
            </Row>
            <Row>
              <Col lg={12} className="noLeftPadding">
                <Row className="marginTop">
                  <Col className="alert-label noLeftPadding" xs={6} md={6} lg={3}>
                    <h6 className="alert-label-header">Source</h6>
                    <div title={source_name} className="alert-label-value">
                      {source_name}
                    </div>
                  </Col>
                  <Col className="alert-label" xs={6} md={6} lg={3}>
                    <h6 className="alert-label-header">Start</h6>
                    <div label={alertDetails.createTime} className="alert-label-value">
                      {alertDetails.createTime}
                    </div>
                  </Col>
                  <Col className="alert-label" xs={6} md={6} lg={3}>
                    <h6 className="alert-label-header">End</h6>
                    <div label={alertDetails.acknowledgedTime} className="alert-label-value">
                      {alertDetails.acknowledgedTime}
                    </div>
                  </Col>
                </Row>
              </Col>
            </Row>
            <Row className="marginTop">
              <Col lg={3} className="alert-label noLeftPadding">
                <h5 className="alert-label-header">status</h5>

                <div className="alert-label-value">{alertDetails.state}</div>
              </Col>
              {alertDetails.state === 'ACTIVE' && (
                <Col lg={3} className="noLeftPadding">
                  <ButtonGroup>
                    <DropdownButton id="alert-mark-as-button" title="Mark as">
                      <MenuItem
                        eventKey="1"
                        onClick={(e) => {
                          e.stopPropagation();
                          onAcknowledge();
                        }}
                      >
                        Acknowledged
                      </MenuItem>
                    </DropdownButton>
                  </ButtonGroup>
                </Col>
              )}
            </Row>
            <Row className="marginTop">
              <Col lg={12} className="noLeftPadding">
                <h5>History</h5>
                <div className="alert-history">
                  <ul>
                    {alertDetails.createTime && (
                      <li className="alert-history-item">
                        <div className="content">
                          <div className="timeline-flow">
                            Alert created on {alertDetails.createTime}
                          </div>
                        </div>
                      </li>
                    )}
                    {alertDetails.acknowledgedTime && (
                      <li className="alert-history-item">
                        <div className="content">
                          Alert acknowledged on {alertDetails.acknowledgedTime}
                        </div>
                      </li>
                    )}
                    {alertDetails.resolvedTime && (
                      <li className="alert-history-item">
                        <div className="content">Alert resolved on {alertDetails.resolvedTime}</div>
                      </li>
                    )}
                  </ul>
                </div>
              </Col>
            </Row>
          </div>
        </div>
      </div>
    );
  }
}
