// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col } from 'react-bootstrap';
import { YBInputField, YBAddRowButton, YBRemoveRowButton, YBSelectWithLabel } from '../../common/forms/fields';
import { Field } from 'redux-form';
import { isValidArray } from '../../../utils/ObjectUtils';
import CollectionSubtypeRow from './CollectionSubtypeRow';

export default class KeyColumnList extends Component {
  static propTypes = {
    columnType: PropTypes.oneOf(['partitionKey', 'clustering', 'other'])
  };

  addKeyItem = () => {
    const {fields} = this.props;
    fields.push({});
  };

  columnListSort = item => {
    const {columnType} = this.props;

    if (columnType === "clustering") {
      const sortOrderOptions = [
        <option key={"ascending"} value={"ASC"}>ASC</option>,
        <option key={"descending"} value={"DESC"}>DESC</option>
      ];
      return (
        <Col xs={2}>
          <Field name={`${item}.sortOrder`} component={YBSelectWithLabel}
                 options={sortOrderOptions} />
        </Col>
      );
    }
  };

  columnTypeChanged = (columnTypeValue, index) => {
    const nextState = this.state;
    nextState.selectedTypes[index] = columnTypeValue;
    this.setState(nextState);
  };

  getAllDataTypes = () => {
    const {tables: {columnDataTypes: {collections, primitives}}} = this.props;
    if (isValidArray(primitives) && isValidArray(collections)) {
      return primitives.concat(collections);
    }
    return [];
  };

  removeKeyItem = indexToRemove => {
    const {fields} = this.props;
    const nextState = this.state;
    nextState.selectedTypes.splice(indexToRemove, 1);
    this.setState(nextState);
    fields.remove(indexToRemove);
  };

  removeRowItem = index => {
    const {columnType} = this.props;
    if (columnType !== "partitionKey" || index > 0) {
      return (
        <YBRemoveRowButton onClick={() => this.removeKeyItem(index)} />
      );
    } else {
      return <span/>;
    }
  };

  componentDidMount() {
    const {fields} = this.props;
    fields.push({});
    this.state = {selectedTypes: []};
  }

  render() {
    const {fields, columnType, tables: {columnDataTypes: {primitives}}} = this.props;
    const allDataTypes = this.getAllDataTypes();
    const typeOptions = CollectionSubtypeRow.getTypeOptions("type", "Type",
      (columnType === "other") ? allDataTypes : primitives
    );

    return (
      <div className="form-field-grid">
        {fields.map((item, index) => (
          <span key={item+index}>
            <Row>
              <Col xs={5}>
                <Field name={`${item}.name`} component={YBInputField} placeHolder={"Column Name"}
                       checkState={true} />
              </Col>
              <Col xs={4}>
                <Field name={`${item}.selected`} options={typeOptions} component={YBSelectWithLabel}
                       placeHolder={"Type"} onInputChanged={(value) => this.columnTypeChanged(value, index)} />
              </Col>
              {this.columnListSort(item)}
              <Col xs={1}>
                {this.removeRowItem(index)}
              </Col>
            </Row>
            <CollectionSubtypeRow name={`${item}.subrow`} tables={this.props.tables} item={item}
                                  columnType={this.state.selectedTypes[index]} />
          </span>
        ))}
        <Row>
          <Col xs={12} className="add-key-column key-row-heading" onClick={this.addKeyItem}>
            <YBAddRowButton btnText="Add Column" />
          </Col>
        </Row>
      </div>
    );
  }
}
