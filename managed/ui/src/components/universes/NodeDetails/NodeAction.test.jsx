import { beforeEach, describe, expect, it, vi } from 'vitest';
import { browserHistory } from 'react-router';
import NodeAction from './NodeAction';

vi.mock('react-router', async (importOriginal) => ({
  ...(await importOriginal()),
  browserHistory: { push: vi.fn() }
}));

const U = '1f497805-ecf9-4300-8f99-31afcd250a88';
const QUERIES_PATH = `/universes/${U}/monitoring/queries`;

// The Actions menu passes these handlers as onClick, so React calls them without `this`.
const clickMenuItem = (handlerName) => {
  const nodeAction = new NodeAction();
  nodeAction.props = { universeUUID: U, currentRow: { name: 'n1' } };
  const onClick = nodeAction[handlerName];
  onClick();
};

describe('NodeAction query menu items', () => {
  beforeEach(() => {
    browserHistory.push.mockClear();
  });

  it('Show Live Queries opens Live Queries for the node', () => {
    clickMenuItem('handleLiveQueryClick');
    expect(browserHistory.push).toHaveBeenCalledWith({
      pathname: QUERIES_PATH,
      query: { tab: 'live-queries', nodeName: 'n1' }
    });
  });

  it('Show Slow Queries opens Slow Queries', () => {
    clickMenuItem('handleSlowQueryClick');
    expect(browserHistory.push).toHaveBeenCalledWith({
      pathname: QUERIES_PATH,
      query: { tab: 'slow-queries' }
    });
  });
});
