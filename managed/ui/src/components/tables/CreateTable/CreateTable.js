// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBInputField, YBButton, YBSelect } from '../../common/forms/fields';
import { Field, FieldArray } from 'redux-form';
import './CreateTables.scss';
import {isValidArray} from '../../../utils/ObjectUtils';

class KeyColumnList extends Component {
  static propTypes = {
    columnType: PropTypes.oneOf(['partitionKey', 'clustering', 'other'])
  };

  constructor(props) {
    super(props);
    this.removeRowItem = this.removeRowItem.bind(this);
    this.removeKeyItem = this.removeKeyItem.bind(this);
    this.columnListSort = this.columnListSort.bind(this);
    this.addKeyItem = this.addKeyItem.bind(this);
  }

  addKeyItem() {
    const {fields} = this.props;
    fields.push({});
  }

  columnListSort(item) {
    const {columnType} = this.props;
    var sortOrderOptions = [<option key={"ascending"} value={"asc"}>
                              asc
                            </option>,
                            <option key={"descending"} value={"desc"}>
                              desc
                            </option>];
    if (columnType === "clustering") {
      return <Col lg={1}><Field name={`${item}.sortOrder`} component={YBSelect} options={sortOrderOptions}/></Col>
    }
  }

  removeRowItem(index) {
    const {columnType} = this.props;
    if (columnType !== "partitionKey" || index > 0) {
      return <YBButton btnClass="btn btn-xs remove-item-btn"
                       btnIcon="fa fa-minus"
                       onClick={()=>this.removeKeyItem(index)} />;
    } else  {
      return <span/>;
    }
  }

  removeKeyItem(index) {
    const {fields} = this.props;
    fields.remove(index);
  }

  componentDidMount() {
    const {fields} = this.props;
    fields.push({});
  }

  render() {
    const {fields, columnType, tables: {columnDataTypes}} = this.props;
    var getFieldLabel = function() {
      if (columnType === "partitionKey") {
        return "Partition Key";
      } else if (columnType === "clustering") {
        return "Clustering";
      } else if (columnType === "other") {
         return "Other";
      } else {
        return "";
      }
    }
    var typeOptions = [<option value="type" key={"type"}>
                         Type
                       </option>];

    if (isValidArray(columnDataTypes)) {
      typeOptions = typeOptions.concat(columnDataTypes.map(function(item, idx){
                     return <option key={idx} value={item}>{item}</option>
                    }));
    }
    return (
      <div className="form-field-grid">
        {fields.map((item, index) =>
          <Row key={item+index}>
            <Col lg={5}>
              <Field
                name={`${item}.name`} component={YBInputField} placeHolder={"Column Name"}
                checkState={true} />
            </Col>
            <Col lg={5}>
              <Field
                name={`${item}.selected`} options={typeOptions}
                component={YBSelect} placeHolder={"Type"}
              />
            </Col>
            {this.columnListSort(item)}
            <Col lg={1} className="text-center">
              {this.removeRowItem(index)}
            </Col>
          </Row>
        )}
        <Row>
          <Col lg={12} className="add-key-column key-row-heading" onClick={this.addKeyItem}>
            <i className="fa fa-plus"></i>&nbsp;Add {getFieldLabel()} Column
          </Col>
        </Row>
      </div>
    )
  }
}

class CassandraColumnSpecification extends Component {
  render () {
    return (
      <div>
        <hr />
        <Row>
          <Col lg={3}>
            <h5 className="no-bottom-margin">Partition Key Columns</h5>
            (In Order)
          </Col>
          <Col lg={9}>
            <FieldArray name="partitionKeyColumns" component={KeyColumnList} columnType={"partitionKey"} {...this.props}/>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col lg={3}>
            <h5 className="no-bottom-margin">Clustering Columns</h5>
            (In Order)
          </Col>
          <Col lg={9}>
            <FieldArray name="clusteringColumns" component={KeyColumnList} columnType={"clustering"} {...this.props}/>
          </Col>
        </Row>
        <hr />
        <Row>
          <Col lg={3}>
            <h5 className="no-bottom-margin">Other Columns</h5>
          </Col>
          <Col lg={9}>
            <FieldArray name="otherColumns" component={KeyColumnList} columnType={"other"} {...this.props} />
          </Col>
        </Row>
      </div>
    )
  }
}
export default class CreateTable extends Component {
  constructor(props) {
    super(props);
    this.createTable = this.createTable.bind(this);
    this.radioClicked = this.radioClicked.bind(this);
  }

  componentWillMount() {
    this.props.fetchTableColumnTypes();
  }

  radioClicked(event) {
    this.setState({'activeTable': event.target.value});
  }

  createTable(values) {
    const {universe: {currentUniverse}} = this.props;
    this.props.submitCreateTable(currentUniverse, values);
  }

  render() {
    const {handleSubmit} = this.props;
    var onFormSubmit = handleSubmit(this.createTable);
    return (
      <div className="bottom-bar-padding">
        <h3>Create Table</h3>
        <form name="CreateTableForm" onSubmit={onFormSubmit}>
          <Row className="create-table-name-container">
            <Col lg={6}>
              <div className="form-right-aligned-labels">
                <Field name="tableName" component={YBInputField} className={`table-name-cell`}
                       label="Name" placeHolder={"Table Name"}/>
              </div>
            </Col>
          </Row>
          <CassandraColumnSpecification {...this.props} />
          <div className="form-action-button-container">
            <YBButton btnText="Create" btnClass={`pull-right btn btn-default bg-orange`} btnType="submit"/>
            <YBButton btnText="Cancel" btnClass={`pull-right btn btn-default`} onClick={this.props.showListTables}/>
          </div>
        </form>
      </div>
    )
  }
}
