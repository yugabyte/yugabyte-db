// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBSelectWithLabel, YBInputField, YBButton } from '../common/forms/fields';
import { Field } from 'redux-form';
import { getPromiseState } from 'utils/PromiseUtils';
import { isNonEmptyObject } from "../../utils/ObjectUtils";
import Highlight from 'react-highlight';
import './Importer.scss';

const stepsEnum = [
  "BEGIN",
  "IMPORTED_MASTERS",
  "IMPORTED_TSERVERS",
  "FINISHED"
];

const stepsHeaders = {
  "BEGIN" : 'Import masters checks',
  "IMPORTED_MASTERS" : 'Import tservers checks',
  "IMPORTED_TSERVERS" : 'Final checks'
};

export default class Importer extends Component {

  constructor(props) {
    super(props);
    this.state = {
      currentState: "BEGIN",
      universeUUID: "",
      checks: {}
    };
  }

  componentWillReceiveProps(nextProps) {
    const { universeImport } = nextProps;

    const importChecksSource  = getPromiseState(universeImport).isError() || (universeImport.data.state !== this.state.currentState && getPromiseState(universeImport).isSuccess())
      ? getPromiseState(universeImport).isSuccess() ? universeImport.data : universeImport.error
      : {};
    const importChecks = isNonEmptyObject(importChecksSource) && ({
      [this.state.currentState] : {...importChecksSource.checks}
    });

    if (getPromiseState(universeImport).isSuccess()) {
      this.setState({
        currentState: universeImport.data.state,
        universeUUID: universeImport.data.universeUUID,
        checks: {
          ...this.state.checks, 
          ...importChecks
        }
      });
    } else {
      this.setState({
        checks: {
          ...this.state.checks, 
          ...importChecks
        }
      });
    }
  }

  submitForm = (formValues) => {
    formValues.currentState = this.state.currentState;
    formValues.universeUUID = this.state.universeUUID;
    return this.props.importUniverse(formValues);
  }

  generateCheckList = () => {
    return isNonEmptyObject(this.state.checks) && Object.keys(this.state.checks).map((section, index) => (
      <div key={section}>
        <h5>{stepsHeaders[section]}</h5>
        <ul>
          {Object.keys(this.state.checks[section]).map((check, idx) => (
            <li key={section.toLowerCase()+"_"+check} className={this.state.checks[section][check]}>
              {/* TODO make checks components */}
              <i className={"fa fa-"+(this.state.checks[section][check]==='OK' ? "check" : "times")}></i>
              {((str) => str.charAt(0).toUpperCase() + str.slice(1))(check.replace(/_/g," ").replace(/db/g,"DB"))}
            </li>))}
        </ul>
      </div>));
  }
  
  generateProgressBar = () => {
    const currentStep = stepsEnum.indexOf(this.state.currentState);
    const barWidth = currentStep === -1 
      ? "0%" 
      : (33.33 *currentStep) +"%";
    const listLabels = stepsEnum.map(function(item, idx) {
      const itemClass = (this.props.universeImport.error && idx === currentStep-1) || (!this.props.universeImport.error && idx === currentStep && currentStep!==0) ? 
        "finished" : 
        (
          this.props.universeImport.error && idx === currentStep+1 ? "failed" : ""
        );
      return (
        <li key={idx} className={itemClass+" "+item.class}><span>{item.replace("_", " ")}</span></li>
      );
    }, this);
    return (
      <ul className="progressbar">
        <div className={"progressbar-bar finished"} style={{ width:barWidth }}></div>
        {listLabels}
      </ul>
    );
    
  }

  render() {
    const {handleSubmit, submitting, universeImport} = this.props;

    const currentStep = stepsEnum.indexOf(this.state.currentState);

    let status = "";
    let errorString = "";
    let submitButtonTxt = "Import Masters";
    if (getPromiseState(universeImport).isSuccess()) {
      // LOGIC get the response from the import and render it in div
      // Master OK
      // 
      if (this.state.currentState === "IMPORTED_MASTERS") {
        status = <span className="yb-success-color">Imported YB-Masters successfully</span>;
        submitButtonTxt = "Import TServers";
      } else if (this.state.currentState === "IMPORTED_TSERVERS") {
        status = <span className="yb-success-color">Imported YB-TServers successfully</span>;
        submitButtonTxt = "Finish Import";
      } else if (this.state.currentState === "FINISHED") {
        status = <span className="yb-success-color">Successfully imported universe</span>;
      }
    } else if (universeImport.error) {
      status = <span className="yb-fail-color">Universe import failed</span>;
      errorString = universeImport.error.error;
      if (this.state.currentState === "IMPORTED_MASTERS") {
        status = <span className="yb-fail-color">Import YB-Masters failed</span>;
        submitButtonTxt = "Import TServers";
      } else if (this.state.currentState === "IMPORTED_TSERVERS") {
        status = <span className="yb-fail-color">Import YB-TServers failed</span>;
        submitButtonTxt = "Finish Import";
      } else if (this.state.currentState === "FINISHED") {
        status = <span className="yb-fail-color">Failed to finalize universe</span>;
      }
    }

    const universeProviderList = [];
    universeProviderList.unshift(<option key="other" value="other">Other</option>);

    return (
      <div className="bottom-bar-padding import-universe-form">
        <form name="ImportUniverse" onSubmit={handleSubmit(this.submitForm)}>
          <Row>
            <h2 className="content-title">Import Existing Universe</h2>
            <Col md={8} sm={12}>
              {this.generateProgressBar()} 
              <div className={"form-right-aligned-labels "+(currentStep > 0 ? "faded" : "")}>
                <Field name="universeName" isReadOnly={currentStep > 0} type="text" component={YBInputField} label="Universe Name" placeHolder="Universe Name"/>
                <Field name="cloudProviderType" type="select" readOnlySelect={currentStep > 0} component={YBSelectWithLabel} label="Cloud Type" options={universeProviderList}/>
                <Field name="masterAddresses" isReadOnly={currentStep > 0}type="text" component={YBInputField} label="Master Addresses" placeHolder="Master Addresses"/>
              </div>
              <YBButton btnText={"Step "+(currentStep+1)+": "+submitButtonTxt} btnType="submit" disabled={submitting || getPromiseState(universeImport).isLoading() } btnClass="btn btn-orange pull-right"/>

              <b className="status">
                {status}
              </b>
              {errorString !=="" && <Highlight className='json'>{errorString}</Highlight>}
              <div className="checks">
                {this.generateCheckList()}
              </div>
            </Col>
          </Row>
        </form>
      </div>
    );
  }
}
