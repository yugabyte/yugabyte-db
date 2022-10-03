import React from 'react';
import ReactDOM from 'react-dom';
import { App } from '@app/App';

ReactDOM.render(<App />, document.getElementById('root'));

// Snowpack's Hot Module Replacement, https://snowpack.dev/concepts/hot-module-replacement
if (import.meta.hot) {
  import.meta.hot.accept();
}
