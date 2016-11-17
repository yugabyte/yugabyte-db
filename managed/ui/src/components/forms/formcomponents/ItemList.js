// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import YBCheckBox from '../../../components/fields/YBCheckBox';
import {Col} from 'react-bootstrap';
import { Field } from 'redux-form';

export default class ItemList extends Component {
  componentDidMount() {
    const {fields, nodeList} = this.props;
    nodeList.forEach(function(item, idx){
      fields.push({});
    });
  }

  render() {
    const {fields, nodeList} = this.props;
    return (
      <div>
        {fields.map((item, index) =>
          <Col lg={4} key={item+index}>
            <Field
            name={item} component={YBCheckBox}
            checkState={true} label={nodeList[index].nodeName}/>
          </Col>
        )}
      </div>
    )
  }
}
