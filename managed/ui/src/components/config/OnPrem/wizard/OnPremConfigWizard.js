// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import './OnPremConfigWizard.scss';
import {OnPremProviderAndAccessKeyContainer, OnPremMachineTypesContainer,
       OnPremRegionsAndZonesContainer } from '../../../config';
import {Row, Col} from 'react-bootstrap';

export default class OnPremConfigWizard extends Component {
  constructor(props) {
    super(props);
    this.nextPage = this.nextPage.bind(this);
    this.prevPage = this.prevPage.bind(this);
    this.state = {currentStep: 0};
  }
  nextPage() {
    this.setState({currentStep: this.state.currentStep + 1})
  }
  prevPage() {
    this.setState({currentStep: this.state.currentStep - 1});
  }
  render() {
    let currentWizardStepContainer = <span/>;
    if (this.state.currentStep === 0) {
      currentWizardStepContainer = <OnPremProviderAndAccessKeyContainer {...this.props} nextPage={this.nextPage}/>;
    } else if (this.state.currentStep === 1) {
      currentWizardStepContainer = <OnPremMachineTypesContainer {...this.props} prevPage={this.prevPage} nextPage={this.nextPage}/>
    } else if (this.state.currentStep === 2) {
      currentWizardStepContainer = <OnPremRegionsAndZonesContainer {...this.props} prevPage={this.prevPage} nextPage={this.nextPage}/>;
    }
    let onPremStepperOptions = ["Provider Info", "Machine Types", "Regions and Zones", "Instances"];
    if (this.props.isEditProvider) {
      onPremStepperOptions = ["Provider Info", "Machine Types", "Regions and Zones"];
    }
    return (
      <div>
        <OnPremStepper currentStep={this.state.currentStep} options={onPremStepperOptions}>
         {currentWizardStepContainer}
        </OnPremStepper>
      </div>
    )
  }
}

class OnPremStepper extends Component {
  render() {
    const {options, currentStep, children} = this.props;
    var optionsArraySize = options.length;
    var cellSize = parseInt(12 / optionsArraySize, 10);
    var cellArray;
    if (currentStep >= optionsArraySize) {
      cellArray = <span/>;
    } else {
      cellArray = options.map(function (item, idx) {
        return (
          <Col lg={cellSize} key={idx} className={`stepper-cell ${(currentStep === idx) ? 'active-stepper-cell' : ''}`}>
            {item}
          </Col>
        )
      });
    }
    return (
      <div>
        <Row className="stepper-container">
          {cellArray}
        </Row>
        {children}
      </div>
    )
  }
}



