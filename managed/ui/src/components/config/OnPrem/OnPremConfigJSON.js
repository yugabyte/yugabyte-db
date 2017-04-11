// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import Highlight from 'react-highlight';
import "highlight.js/styles/github.css";
import AceEditor from 'react-ace';
import 'brace/theme/github';
import {YBPanelItem} from '../../panels'
import { YBButton } from '../../common/forms/fields';
import sampleDataCenterConfig from '../templates/SampleDataCenterConfig.json';

class ConfigFormTitle extends Component {
  render() {
    const { titleText, copyTextToForm} = this.props;
    return (
      <div className="sample-config-item-label">
        <Col lg={10} className="color-grey">{titleText}</Col>
        <Col lg={2}>
            <YBButton btnIcon="fa fa-files-o" onClick={copyTextToForm}/>
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
    const configTitle = "Enter your DataCenter Config here";
    return (
      <Row className="form-data-container">
        <Col lg={5} className="sample-config-item">
          <div className="color-light-grey">
            <ConfigFormTitle text={this.sampleJsonPretty}
                             titleText={"Sample DataCenter Config"}
                             copyTextToForm={this.copyTextToForm}/>
          </div>
          <Highlight className='json'>{this.sampleJsonPretty}</Highlight>
        </Col>
        <Col lg={5} id="sample-panel-item">
          <YBPanelItem name={configTitle} hideToolBox={true}>
            <AceEditor
              theme="github"
              onChange={this.onChange}
              name="dc-config-val"
              value={this.props.configJsonVal}
            />
          </YBPanelItem>
        </Col>
      </Row>
    )
  }
}
