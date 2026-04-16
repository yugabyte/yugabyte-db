import _ from 'lodash';
import { Route, Router } from 'react-router-dom';
import { browserHistory } from 'react-router';
import userEvent from '@testing-library/user-event';
import { createMemoryHistory } from 'history';
import { Administration } from './Administration';
import { render } from '../../test-utils';

jest.mock('../alerts/AlertConfiguration/AlertConfigurationContainer.js');
jest.mock('../users/Users/UsersListContainer.js');
jest.mock('../advanced/RuntimeConfig/RuntimeConfigContainer.tsx');

const setup = (storeState = {}) => {
  const history = createMemoryHistory();
  history.push('/admin');
  const component = render(
    <Router history={history}>
      <Route path="/admin" component={Administration}>
        <Route path="/admin/:tab/:section" component={Administration} />
      </Route>
    </Router>,
    { storeState }
  );
  return { component, history };
};

describe('Administration page', () => {
  xit('should render successfully and have correct tabs selected', () => {
    const { component } = setup();
    expect(
      component.getByRole('tab', { name: /high availability/i, selected: true })
    ).toBeInTheDocument();
    expect(
      component.getByRole('tab', { name: /replication configuration/i, selected: true })
    ).toBeInTheDocument();
    expect(
      component.getByRole('tab', { name: /instance configuration/i, selected: false })
    ).toBeInTheDocument();
  });

  xit('should switch to instances tab on route change', () => {
    const { component, history } = setup();
    history.push('/admin/ha/instances');
    expect(
      component.getByRole('tab', { name: /replication configuration/i, selected: false })
    ).toBeInTheDocument();
    expect(
      component.getByRole('tab', { name: /instance configuration/i, selected: true })
    ).toBeInTheDocument();
  });

  xit('should switch to instances tab by mouse click', () => {
    const { component } = setup();
    userEvent.click(component.getByRole('tab', { name: /instance configuration/i }));
    expect(
      component.getByRole('tab', { name: /replication configuration/i, selected: false })
    ).toBeInTheDocument();
    expect(
      component.getByRole('tab', { name: /instance configuration/i, selected: true })
    ).toBeInTheDocument();
  });

  xit('should redirect to root route when user has insufficient permissions', () => {
    const browserHistoryPush = jest.fn();
    jest.spyOn(browserHistory, 'push').mockImplementation(browserHistoryPush);
    const store = _.set(
      {},
      'customer.currentCustomer.data.features.menu.administration',
      'disabled'
    );
    setup(store);
    expect(browserHistoryPush).toBeCalledWith('/');
  });
});
