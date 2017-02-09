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

  componentWillMount() {
    this.state = {configJsonVal: JSON.stringify(this.props.config.onPremJsonFormData, null, 2)};
  }
  componentWillUnmount() {
    if (this.state.configJsonVal.length > 0) {
      this.props.setOnPremJsonData(JSON.parse(this.state.configJsonVal));
    }
  }
  render() {
    var self = this;
    var jsonPretty = JSON.stringify(JSON.parse(JSON.stringify(sampleDataCenterConfig)), null, 2);
    var configTitle = "Enter your DataCenter Config here";
    function onChange(newValue) {
      self.setState({configJsonVal: newValue});
    }
    function copyTextToForm() {
      self.setState({configJsonVal: jsonPretty});
    }
    return (
      <Row className="form-data-container">
        <Col lg={5} className="sample-config-item">
          <div className="color-light-grey">
            <ConfigFormTitle text={jsonPretty} titleText={"Sample DataCenter Config"} copyTextToForm={copyTextToForm}/>
          </div>
          <Highlight className='json'>{jsonPretty}</Highlight>
        </Col>
        <Col lg={5} id="sample-panel-item">
          <YBPanelItem name={configTitle} hideToolBox={true}>
            <AceEditor
              theme="github"
              onChange={onChange}
              name="dc-config-val"
              value={self.state.configJsonVal}
            />
          </YBPanelItem>
        </Col>
      </Row>
    )
  }
}
