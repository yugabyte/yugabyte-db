import React, { Component } from 'react';
import {Grid, Row, Col } from 'react-bootstrap';
import YBPanelItem from '../YBPanelItem';
import YBButton from '../../components/fields/YBButton';
import Highlight from 'react-highlight';
import "highlight.js/styles/github.css";
import AceEditor from 'react-ace';
import 'brace/mode/json';
import 'brace/theme/github';
import sampleDataCenterConfig from '../../templates/SampleDataCenterConfig.json';
class SetupDataCenterFormTitle extends Component {
  render() {
    const { titleText, copyTextToForm} = this.props;
    return (
      <span>
        <Col lg={10} className="color-grey">{titleText}</Col>
        <Col lg={2}>

            <YBButton btnIcon="fa fa-files-o" onClick={copyTextToForm}/>

        </Col>
      </span>
    )
  }
}

export default class SetupDataCenterForm extends Component {
  constructor(props) {
    super(props);
    this.state= {configJsonVal: ''};
  }
  render() {
    var self = this;
    var jsonPretty = JSON.stringify(JSON.parse(JSON.stringify(sampleDataCenterConfig)), null, 2);
    var configTitle = "Enter your DataCenter Config here";
    function onChange(newValue) {
      self.setState({configJsonVal: newValue});
    }
    function submitConfigValue() {
      self.props.submitDCConfig(self.state.configJsonVal);
    }
    function copyTextToForm() {
      self.setState({configJsonVal: jsonPretty});
    }
    return (
      <div id="page-wrapper">
        <Col lg={10} lgOffset={1}>
          <Grid>
            <Row>
              <h2 className="text-center">Setup Your DataCenter</h2>
            </Row>
            <Col lg={8} className="sample-panel-item" id="sample-panel-item">
              <YBPanelItem name={configTitle} hideToolBox={true}>
                <AceEditor
                  mode="json"
                  theme="github"
                  onChange={onChange}
                  name="dc-config-val"
                  value={self.state.configJsonVal}
                />
              </YBPanelItem>
            </Col>
            <Col lg={4} className="sample-panel-item">
              <div className="color-light-grey">
                <SetupDataCenterFormTitle text={jsonPretty} titleText={"Sample DataCenter Config"} copyTextToForm={copyTextToForm}/>
              </div>
              <Highlight className='json' id="foo">{jsonPretty}</Highlight>
            </Col>
            <YBButton btnClass="btn btn-default btn-center bg-orange" btnText="Submit" onClick={submitConfigValue}/>
          </Grid>
        </Col>
      </div>
    )
  }
}
