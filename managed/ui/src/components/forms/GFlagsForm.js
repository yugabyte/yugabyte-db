import React, { Component, PropTypes } from 'react';
import YBButton from './../fields/YBButton';
import { Field, FieldArray } from 'redux-form';
import YBInputField from '../../components/YBInputField'
import {Row, Col} from 'react-bootstrap';
import YBModal from '../../components/fields/YBModal';

class FlagInput extends Component {

  render() {
    const {deleteRow, item} = this.props;

    return (
      <Row>
        <Col lg={5}>
          <Field name={`${item}.name`} component={YBInputField} className="input-sm" placeHolder="GFlag Name"/>
        </Col>
        <Col lg={5}>
          <Field name={`${item}.value`} component={YBInputField} className="input-sm" placeHolder="Value"/>
        </Col>
        <Col lg={1}>
          <YBButton btnSize="sm" btnIcon="fa fa-times fa-fw" onClick={deleteRow}/>
        </Col>
      </Row>
    )
  }
}
class FlagItems extends Component {
  componentDidMount() {
    this.props.fields.push({});
  }
  render() {
    const { fields } = this.props;
    var addFlagItem = function() {
      fields.push({})
    }
    var gFlagsFieldList = fields.map(function(item, idx){
      return <FlagInput item={item} key={idx}
                        deleteRow={() => fields.remove(idx)} />
    })

    return (
      <div>
        {
          gFlagsFieldList
        }
        <YBButton btnClass="btn btn-sm universe-btn btn-default"
                  btnText="Add" btnIcon="fa fa-plus"
                  onClick={addFlagItem} />
      </div>
    )
  }
}

export default class GFlagsForm extends Component {
  constructor(props) {
    super(props);
    this.submitGFlagsForm = this.props.submitGFlagsForm.bind(this);
  }

  render() {
    const {onHide, modalVisible, handleSubmit} = this.props;
    const submitAction = handleSubmit(this.submitGFlagsForm);

    return (
      <YBModal size="small" visible={modalVisible} formName={"GFlagsForm"}
               onHide={onHide} title="Set GFlags" onFormSubmit={submitAction}>
        <FieldArray name="gFlags" component={FlagItems}/>
      </YBModal>
    )
  }
}

GFlagsForm.propTypes = {
  "modalVisible": PropTypes.bool.isRequired,
  "onHide": PropTypes.func.isRequired
}
