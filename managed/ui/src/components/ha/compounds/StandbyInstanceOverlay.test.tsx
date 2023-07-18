import { createMemoryHistory } from 'history';
import { Route, Router } from 'react-router-dom';
import { render } from '../../../test-utils';
import { useLoadHAConfiguration } from '../hooks/useLoadHAConfiguration';
import { StandbyInstanceOverlay } from './StandbyInstanceOverlay';
import { HAConfig } from '../../../redesign/helpers/dtos';

jest.mock('../hooks/useLoadHAConfiguration');

type HookReturnType = Partial<ReturnType<typeof useLoadHAConfiguration>>;

const fakeStandbyConfig = { instances: [{ is_local: true, is_leader: false }] } as HAConfig;
const fakeActiveConfig = { instances: [{ is_local: true, is_leader: true }] } as HAConfig;

const setup = (hookResponse: HookReturnType, route = '/') => {
  (useLoadHAConfiguration as jest.Mock<HookReturnType>).mockReturnValue(hookResponse);
  const history = createMemoryHistory();
  history.push(route);
  return render(
    <Router history={history}>
      <Route path={route} component={StandbyInstanceOverlay} />
    </Router>
  );
};

describe('HA overlay', () => {
  it('should render overlay for standby instance', () => {
    const component = setup({ config: fakeStandbyConfig });
    expect(component.container).not.toBeEmptyDOMElement();
  });

  it('should not render overlay for active instance', () => {
    const component = setup({ config: fakeActiveConfig });
    expect(component.container).toBeEmptyDOMElement();
  });

  it('should not render overlay for special routes', () => {
    let component = setup({ config: fakeStandbyConfig }, '/admin/ha/any-other-text-here');
    expect(component.container).toBeEmptyDOMElement();

    component = setup({ config: fakeStandbyConfig }, '/logs');
    expect(component.container).toBeEmptyDOMElement();
  });

  it('should not render overlay when there is no HA config yet', () => {
    let component = setup({ isNoHAConfigExists: true });
    expect(component.container).toBeEmptyDOMElement();

    component = setup({ isLoading: true });
    expect(component.container).toBeEmptyDOMElement();

    component = setup({ error: true });
    expect(component.container).toBeEmptyDOMElement();
  });
});
