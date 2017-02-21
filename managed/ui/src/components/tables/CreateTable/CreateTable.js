// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Row, Col } from 'react-bootstrap';
import { YBInputField, YBButton, YBSelect } from '../../common/forms/fields';
import { Field, FieldArray } from 'redux-form';
import { YBPanelItem } from '../../panels';
import './CreateTables.scss';
import { DescriptionItem } from '../../common/descriptors';
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
    if (columnType !== "partitionKey") {
      return <Col lg={2}><Field name={`${item}.sortOrder`} component={YBSelect} options={sortOrderOptions}/></Col>
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
      <div>
        {fields.map((item, index) =>
          <Row key={item+index}>
            <Col lg={4}>
              <Field
                name={`${item}.name`} component={YBInputField} placeHolder={"Column Name"}
                checkState={true} />
            </Col>
            <Col lg={2}>
              <Field
                name={`${item}.selected`} options={typeOptions}
                component={YBSelect} placeHolder={"Type"}
              />
            </Col>
            {this.columnListSort(item)}
            <Col lg={1}>
              {this.removeRowItem(index)}
            </Col>
          </Row>
        )}
        <div className="add-key-column key-row-heading" onClick={this.addKeyItem}>
          <i className="fa fa-plus"></i>&nbsp;Add {getFieldLabel()} Column
        </div>
      </div>
    )
  }
}

class CassandraColumnSpecification extends Component {
  render () {
    return (
      <div>
        <Row>
          <Col lg={3}>
            <DescriptionItem title="Partition Key Column">
              <span>(In Order)</span>
            </DescriptionItem>
          </Col>
          <Col lg={9}>
            <FieldArray name="partitionKeyColumns" component={KeyColumnList} columnType={"partitionKey"} {...this.props}/>
          </Col>
        </Row>
        <Row>
          <Col lg={3}>
            <DescriptionItem title="Clustering Columns">
              <span>(In Order)</span>
            </DescriptionItem>
          </Col>
          <Col lg={9}>
            <FieldArray name="clusteringColumns" component={KeyColumnList} columnType={"clustering"} {...this.props}/>
          </Col>
        </Row>
        <Row className="other-column-container">
          <Col lg={3}>
            <DescriptionItem title="Other Columns">
              <span/>
            </DescriptionItem>
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
      <YBPanelItem name="Create Table">
        <form name="CreateTableForm" onSubmit={onFormSubmit}>
          <Row className="create-table-name-container">
            <Col lg={6}>
              <Field name="tableName" component={YBInputField} className={`table-name-cell`}
                     label="Name" placeHolder={"Table Name"}/>
            </Col>
          </Row>
          <CassandraColumnSpecification {...this.props} />
          <Row>
            <Col lg={2} lgOffset={10}>
              <YBButton btnText="Cancel" btnClass={`btn bg-grey table-btn`} onClick={this.props.showListTables}/>
              <YBButton btnText="Create" btnClass={`btn bg-orange table-btn`} btnType="submit"/>
            </Col>
          </Row>
        </form>
      </YBPanelItem>
    )
  }
}
