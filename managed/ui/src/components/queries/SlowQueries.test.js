import React from 'react';
import axios from 'axios';
import { render, screen, waitFor } from './helpers/test-utils';
import userEvent from '@testing-library/user-event';
import { within } from '@testing-library/dom';
import { mockSlowYsqlQueries } from './helpers/mockQueryData';
import { SlowQueries } from './SlowQueries';

jest.mock('axios');
jest.mock('../../pages/Login');

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

describe('Query search input tests', () => {
  it('render all columns in autocomplete dropdown', async () => {
    userEvent.click(screen.getByRole('textbox'));
    await waitFor(() => {
      const searchBar = document.getElementById('slow-query-search-bar');
      expect(within(searchBar).getAllByRole('listitem')).toHaveLength(11);
    });
  });

  it('render columns in autocomplete that match user input', async () => {
    userEvent.click(screen.getByRole('textbox'));
    userEvent.type(screen.getByRole('textbox'), 'time');
    await waitFor(() => {
      const searchBar = document.getElementById('slow-query-search-bar');
      expect(within(searchBar).getAllByRole('listitem')).toHaveLength(5);
    });
    userEvent.type(screen.getByRole('textbox'), '{arrowdown}{arrowdown}{arrowdown}{enter}');
    await waitFor(() => {
      expect(screen.getByRole('textbox')).toHaveValue('Min Time:');
    });
  });

  it('test dropdown shows after entering search term', async () => {
    const table = document.getElementsByClassName('slow-queries__table')[0];
    expect(within(table).getAllByRole('row')).toHaveLength(3);
    userEvent.click(screen.getByRole('textbox'));
    userEvent.type(screen.getByRole('textbox'), 'Count:>10{enter}');
    await waitFor(() => {
      expect(screen.getByRole('textbox')).toHaveFocus();
      expect(within(table).getAllByRole('row')).toHaveLength(2);
    });
  });
});
