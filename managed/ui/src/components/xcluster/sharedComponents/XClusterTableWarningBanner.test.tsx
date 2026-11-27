import { render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { ThemeProvider } from '@material-ui/core';
import { YBThemeProvider, yba } from '@yugabyte-ui-library/core';
import i18n from 'i18next';
import { initReactI18next } from 'react-i18next';
import { beforeAll, describe, expect, it, vi } from 'vitest';

import { mainTheme } from '@app/redesign/theme/mainTheme';
import en from '@app/translations/en.json';
import { TableCountsOfConcern } from '../ReplicationUtils';
import {
  XClusterTableWarningBanner,
  XCLUSTER_TABLE_WARNING_BANNER_TEST_ID
} from './XClusterTableWarningBanner';

const tableCounts = (overrides: Partial<TableCountsOfConcern> = {}): TableCountsOfConcern => ({
  unableToFetchStatus: 0,
  failedReplicationSetup: 0,
  unknownError: 0,
  restartRequired: 0,
  autoFlagMismatched: 0,
  dbSchemaMismatched: 0,
  otherError: 0,
  uniqueTableCount: 0,
  ...overrides
});

const renderBanner = ({
  tableCountsOfConcern = tableCounts(),
  numTablesAboveLagThreshold = 0,
  isDrInterface = true,
  onViewTables = vi.fn()
}: {
  tableCountsOfConcern?: TableCountsOfConcern;
  numTablesAboveLagThreshold?: number;
  isDrInterface?: boolean;
  onViewTables?: () => void;
} = {}) =>
  render(
    <YBThemeProvider theme={yba.ybaTheme}>
      <ThemeProvider theme={mainTheme}>
        <XClusterTableWarningBanner
          tableCountsOfConcern={tableCountsOfConcern}
          numTablesAboveLagThreshold={numTablesAboveLagThreshold}
          isDrInterface={isDrInterface}
          onViewTables={onViewTables}
          restartButton={<button type="button">restart</button>}
        />
      </ThemeProvider>
    </YBThemeProvider>
  );

const getBanner = () => screen.getByTestId(XCLUSTER_TABLE_WARNING_BANNER_TEST_ID);

describe('XClusterTableWarningBanner', () => {
  beforeAll(async () => {
    await i18n.use(initReactI18next).init({
      resources: { en },
      lng: 'en',
      interpolation: { escapeValue: false }
    });
  });

  it('renders nothing when no table needs attention', () => {
    renderBanner();
    expect(screen.queryByTestId(XCLUSTER_TABLE_WARNING_BANNER_TEST_ID)).not.toBeInTheDocument();
  });

  it('shows a lag warning naming the offering when only lag is above threshold', () => {
    renderBanner({ numTablesAboveLagThreshold: 2, isDrInterface: false });
    expect(getBanner()).toHaveAttribute('aria-label', 'banner warning');
    expect(getBanner()).toHaveTextContent('xCluster replication lag is above the alert threshold.');
  });

  it('shows an error banner without the restart step when no table is missing an op', async () => {
    const onViewTables = vi.fn();
    renderBanner({
      tableCountsOfConcern: tableCounts({ dbSchemaMismatched: 1, uniqueTableCount: 1 }),
      numTablesAboveLagThreshold: 3,
      onViewTables
    });
    expect(getBanner()).toHaveAttribute('aria-label', 'banner error');
    expect(getBanner()).toHaveTextContent(
      'Some relevant tables may require your attention. Check the status of tables in replication.'
    );
    expect(getBanner()).toHaveTextContent(
      'For information on the table statuses and how to resolve them, refer to documentation.'
    );
    expect(screen.getByRole('link', { name: 'documentation' })).toBeInTheDocument();
    expect(screen.queryByText('restart')).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: 'View Tables' })).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole('button', { name: 'status of tables' }));
    expect(onViewTables).toHaveBeenCalledTimes(1);
  });

  it('shows the restart step when a table is missing an op', () => {
    renderBanner({
      tableCountsOfConcern: tableCounts({ restartRequired: 1, uniqueTableCount: 1 })
    });
    expect(getBanner()).toHaveTextContent('Missing Op ID');
    expect(screen.getByRole('button', { name: 'restart' })).toBeInTheDocument();
    expect(screen.getByRole('link', { name: 'documentation' })).toHaveAttribute(
      'href',
      expect.stringContaining('disaster-recovery-setup/#status')
    );
  });

  it('calls onViewTables from the lag warning action', async () => {
    const onViewTables = vi.fn();
    renderBanner({ numTablesAboveLagThreshold: 1, onViewTables });
    await userEvent.click(screen.getByRole('button', { name: 'View Tables' }));
    expect(onViewTables).toHaveBeenCalledTimes(1);
  });
});
