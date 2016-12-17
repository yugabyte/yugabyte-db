// Copyright YugaByte Inc.

import { connect } from 'react-redux';
import { CreateTable } from '../../components/tables';
import { reduxForm } from 'redux-form';
import { createUniverseTable, createUniverseTableFailure, createUniverseTableSuccess } from '../../actions/tables'

const mapDispatchToProps = (dispatch) => {
  return {
    submitCreateTable: (universeUUID, values) => {
      dispatch(createUniverseTable(universeUUID, values)).then((response) => {
        if (response.payload.status !== 200) {
          dispatch(createUniverseTableFailure(response.payload));
        } else {
          dispatch(createUniverseTableSuccess(response.payload));
        }
      });
    }
  }
}

var createTableForm = reduxForm({
  form: 'CreateTableForm'
})


function mapStateToProps(state) {
  return {
    universe: state.universe,
    tables: state.tables
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(createTableForm(CreateTable));
