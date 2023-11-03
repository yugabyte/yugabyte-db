import axios from 'axios';
import { render, screen } from '../../test-utils';
import userEvent from '@testing-library/user-event';
import { within } from '@testing-library/dom';
import { mockSlowYsqlQueries } from './helpers/mockQueryData';
import { SlowQueries } from './SlowQueries';

jest.mock('axios');

beforeEach(() => {
  const ysqlQueries = {
    queries: mockSlowYsqlQueries
  };
  const resp = {
    data: {
      ysql: ysqlQueries
    }
  };
  axios.get.mockResolvedValue(resp);
  render(<SlowQueries />);
});

// TODO: fix and un-skip
describe.skip('Query search input tests', () => {
  it('render all columns in autocomplete dropdown', () => {
    userEvent.click(screen.getByRole('textbox'));
    const searchBar = document.getElementById('slow-query-search-bar');
    expect(within(searchBar).getAllByRole('listitem')).toHaveLength(11);
  });

  it('render columns in autocomplete that match user input', () => {
    userEvent.click(screen.getByRole('textbox'));
    userEvent.type(screen.getByRole('textbox'), 'time');
    const searchBar = document.getElementById('slow-query-search-bar');
    expect(within(searchBar).getAllByRole('listitem')).toHaveLength(5);

    userEvent.type(screen.getByRole('textbox'), '{arrowdown}{arrowdown}{arrowdown}{enter}');
    expect(screen.getByRole('textbox')).toHaveValue('Min Time:');
  });

  it('test dropdown shows after entering search term', () => {
    const table = document.getElementsByClassName('slow-queries__table')[0];
    expect(within(table).getAllByRole('row')).toHaveLength(3);
    userEvent.click(screen.getByRole('textbox'));
    userEvent.type(screen.getByRole('textbox'), 'Count:>10{enter}');
    expect(screen.getByRole('textbox')).toHaveFocus();
    expect(within(table).getAllByRole('row')).toHaveLength(2);
  });
});
