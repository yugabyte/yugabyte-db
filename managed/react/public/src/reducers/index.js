import { combineReducers } from 'redux';
import CustomerReducer from './reducer_customer';
import { reducer as formReducer } from 'redux-form';

const rootReducer = combineReducers({
  customer: CustomerReducer,
  form: formReducer
});

export default rootReducer;
