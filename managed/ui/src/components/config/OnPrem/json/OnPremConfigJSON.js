// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import Highlight from 'react-highlight';
import "highlight.js/styles/github.css";
import AceEditor from 'react-ace';
import 'brace/theme/github';
import {YBPanelItem} from '../../../panels'
import { YBButton } from '../../../common/forms/fields';
import sampleDataCenterConfig from '../../templates/SampleDataCenterConfig.json';

class ConfigFormTitle extends Component {
  render() {
    const { titleText, copyTextToForm} = this.props;
    return (
      <div className="sample-config-item-label">
        <Col lg={9} className="color-grey">{titleText}</Col>
        <Col lg={3} className="text-right">
            <YBButton btnIcon="fa fa-files-o" btnText={"Copy"} onClick={copyTextToForm}/>
        </Col>
      </div>
    )
  }
}

export default class OnPremConfigJSON extends Component {

  constructor(props) {
    super(props);
    this.sampleJsonPretty = JSON.stringify(JSON.parse(JSON.stringify(sampleDataCenterConfig)), null, 2);
    this.onChange = this.onChange.bind(this);
    this.copyTextToForm = this.copyTextToForm.bind(this);
  }

  componentWillUnmount() {
    if (this.props.configJsonVal.length > 0) {
      this.props.setOnPremJsonData(JSON.parse(this.props.configJsonVal));
    }
  }

  onChange(newValue) {
    this.props.updateConfigJsonVal(newValue);
  }

  copyTextToForm() {
    this.props.updateConfigJsonVal(this.sampleJsonPretty);
  }

  render() {
    const configTitle = "Enter Datacenter Configuration JSON:";
    const {switchToWizardEntry, submitJson} = this.props;
    return (
      <div>
        <Row className="form-data-container">
        <Col lg={5} className="sample-config-item">
          <Row className="color-light-grey">
            <ConfigFormTitle text={this.sampleJsonPretty}
                             titleText={"Example Datacenter Configuration"}
                             copyTextToForm={this.copyTextToForm}/>
          </Row>
          <Highlight className='json'>{this.sampleJsonPretty}</Highlight>
        </Col>
        <Col lg={5} id="sample-panel-item">
          <YBPanelItem name={configTitle} hideToolBox={true}>
            <AceEditor
              theme="github"
              onChange={this.onChange}
              name="dc-config-val"
              value={this.props.configJsonVal}
              height="470px"
              width="560px"
            />
          </YBPanelItem>
        </Col>
      </Row>
        <Row>
          {switchToWizardEntry}
          <YBButton btnText={"Submit"} btnType={"submit"} btnClass={"btn btn-default save-btn pull-right"} onClick={submitJson}/>
        </Row>
      </div>
    )
  }
}
