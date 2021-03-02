import React from 'react'
import { render } from '@testing-library/react'
import { createStore } from 'redux';
import { QueryClient, QueryClientProvider } from 'react-query';
import { Provider } from 'react-redux';
import rootReducer from '../../../reducers';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false,
      refetchOnWindowFocus: false
    }
  }
});

const ConnectedComponent = ({ children }) => {
  let store = createStore(rootReducer, {});
  return (
    <Provider store={store}>
      <QueryClientProvider client={queryClient}>
        {children}
      </QueryClientProvider>
    </Provider>
  );
}

const customRender = (ui, options) =>
  render(ui, { wrapper: ConnectedComponent, ...options })

// re-export everything
export * from '@testing-library/react'

// override render method
export { customRender as render }