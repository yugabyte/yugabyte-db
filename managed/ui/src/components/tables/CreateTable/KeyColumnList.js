// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { Row, Col } from 'react-bootstrap';
import { YBInputField, YBButton, YBSelectWithLabel } from '../../common/forms/fields';
import { Field } from 'redux-form';
import { isValidArray } from '../../../utils/ObjectUtils';
import CollectionSubtypeRow from './CollectionSubtypeRow';

export default class KeyColumnList extends Component {

  static propTypes = {
    columnType: PropTypes.oneOf(['partitionKey', 'clustering', 'other'])
  };

  constructor(props) {
    super(props);
    this.addKeyItem = this.addKeyItem.bind(this);
    this.columnListSort = this.columnListSort.bind(this);
    this.columnTypeChanged = this.columnTypeChanged.bind(this);
    this.getAllDataTypes = this.getAllDataTypes.bind(this);
    this.removeKeyItem = this.removeKeyItem.bind(this);
    this.removeRowItem = this.removeRowItem.bind(this);
  }

  addKeyItem() {
    const {fields} = this.props;
    fields.push({});
  }

  columnListSort(item) {
    const {columnType} = this.props;

    if (columnType === "clustering") {
      const sortOrderOptions = [
        <option key={"ascending"} value={"ASC"}>ASC</option>,
        <option key={"descending"} value={"DESC"}>DESC</option>
      ];
      return (
        <Col md={2}>
          <Field name={`${item}.sortOrder`} component={YBSelectWithLabel}
                 options={sortOrderOptions} />
        </Col>
      );
    }
  }

  columnTypeChanged(columnTypeValue, index) {
    const nextState = this.state;
    nextState.selectedTypes[index] = columnTypeValue;
    this.setState(nextState);
  }

  getAllDataTypes() {
    const {tables: {columnDataTypes: {collections, primitives}}} = this.props;
    if (isValidArray(primitives) && isValidArray(collections)) {
      return primitives.concat(collections);
    }
    return [];
  }

  removeKeyItem(indexToRemove) {
    const {fields} = this.props;
    const nextState = this.state;
    nextState.selectedTypes.splice(indexToRemove, 1);
    this.setState(nextState);
    fields.remove(indexToRemove);
  }

  removeRowItem(index) {
    const {columnType} = this.props;
    if (columnType !== "partitionKey" || index > 0) {
      return (
        <YBButton btnClass="btn btn-xs remove-item-btn"
                  btnIcon="fa fa-minus"
                  onClick={() => this.removeKeyItem(index)} />
      );
    } else {
      return <span/>;
    }
  }

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
    const getFieldLabel = function() {
      if (columnType === "partitionKey") {
        return "Partition Key";
      } else if (columnType === "clustering") {
        return "Clustering";
      } else if (columnType === "other") {
        return "Other";
      } else {
        return "";
      }
    };

    return (
      <div className="form-field-grid">
        {fields.map((item, index) => (
          <span key={item+index}>
            <Row>
              <Col md={5}>
                <Field name={`${item}.name`} component={YBInputField} placeHolder={"Column Name"}
                       checkState={true} />
              </Col>
              <Col md={4}>
                <Field name={`${item}.selected`} options={typeOptions} component={YBSelectWithLabel}
                       placeHolder={"Type"} onInputChanged={(value) => this.columnTypeChanged(value, index)} />
              </Col>
              {this.columnListSort(item)}
              <Col md={1} className="text-center">
                {this.removeRowItem(index)}
              </Col>
            </Row>
            <CollectionSubtypeRow name={`${item}.subrow`} tables={this.props.tables} item={item}
                                  columnType={this.state.selectedTypes[index]} />
          </span>
        ))}
        <Row>
          <Col md={12} className="add-key-column key-row-heading" onClick={this.addKeyItem}>
            <i className="fa fa-plus" />&nbsp;Add {getFieldLabel()} Column
          </Col>
        </Row>
      </div>
    );
  }
}
