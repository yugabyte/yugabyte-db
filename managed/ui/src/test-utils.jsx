import { render } from '@testing-library/react';
import { createStore } from 'redux';
import { QueryClient, QueryClientProvider } from 'react-query';
import { Provider } from 'react-redux';
import rootReducer from './reducers';

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: false,
      refetchOnWindowFocus: false
    }
  }
});

const customRender = (ui, options) => {
  const { storeState = {}, ...renderOptions } = options || {};
  const store = createStore(rootReducer, storeState);

  const Wrapper = ({ children }) => (
    <Provider store={store}>
      <QueryClientProvider client={queryClient}>
        {children}
      </QueryClientProvider>
    </Provider>
  );

  return render(ui, { wrapper: Wrapper, ...renderOptions });
};

// re-export everything
export * from '@testing-library/react';

// override render method
export { customRender as render };
