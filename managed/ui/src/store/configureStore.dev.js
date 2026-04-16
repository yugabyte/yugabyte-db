// Copyright (c) YugabyteDB, Inc.

import { createStore, applyMiddleware, compose } from 'redux';
import promise from 'redux-promise';
import reducer from '../reducers';

export default function configureStore(initialState) {
  const finalCreateStore = compose(
    applyMiddleware(promise),
    window.__REDUX_DEVTOOLS_EXTENSION__ ? window.__REDUX_DEVTOOLS_EXTENSION__() : (f) => f
  )(createStore);

  const store = finalCreateStore(reducer, initialState);

  if (import.meta.hot) {
    import.meta.hot.accept('../reducers', (newModule) => {
      const nextReducer = newModule.default;
      store.replaceReducer(nextReducer);
    });
  }

  return store;
}
