import { render, screen } from '../../../test-utils';
import { within } from '@testing-library/dom';
import userEvent from '@testing-library/user-event';
import { IntlProvider } from 'react-intl';

import { mockProps } from './UniverseViewMockData';
import { UniverseView } from '../../universes';

describe('UniverseView render basic test', () => {
  const universeCount = mockProps.universe.universeList.data.length;

  beforeEach(() => {
    render(
      <IntlProvider locale="en">
        <UniverseView {...mockProps} />
      </IntlProvider>
    );
  });
  it('renders the list of universes in list view', () => {
    const universeList = screen.getByRole('list', { name: /universe list/i });
    const { getAllByRole } = within(universeList);
    const universeItems = getAllByRole('listitem');

    expect(universeList).toBeInTheDocument();
    expect(universeItems.length).toEqual(universeCount);
  });
  it('triggers view change using view toggle', () => {
    // Assuming list view is the default view
    let viewChangeButton = screen.getByRole('button', { name: /table view/i });
    let universeList = screen.getByRole('list', { name: /universe list/i });
    let universeTable = screen.queryAllByRole('table');
    expect(universeList).toBeInTheDocument();
    expect(universeTable).toHaveLength(0);

    // Change to table view
    userEvent.click(viewChangeButton);
    universeTable = screen.getAllByRole('table');
    const universeRows = screen.getAllByRole('row');
    // "1" in tests below corresponds to table header row

    expect(universeList).not.toBeInTheDocument();
    expect(universeTable.length).not.toEqual(0);
    expect(universeRows).toHaveLength(1 + universeCount);

    viewChangeButton = screen.getByRole('button', { name: /list view/i });
    // Change back to list view
    userEvent.click(viewChangeButton);
    universeList = screen.getByRole('list', { name: /universe list/i });
    universeTable = screen.queryAllByRole('table');

    universeTable = screen.queryAllByRole('table');
    expect(universeList).toBeInTheDocument();
    expect(universeTable).toHaveLength(0);
  });
});
