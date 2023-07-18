import { fireEvent } from '@testing-library/dom';
import { render, screen } from '../../../test-utils';
import { BackupList } from './BackupList';

const backupListProps = {
  activeTab: 's3',
  data: [
    {
      configName: 'S3-Default',
      configUUID: '1fbda868-d39d-4d76-9b15-a056d0c7bc28',
      customerUUID: 'f33e3c9b-75ab-4c30-80ad-cba85646ea39',
      data: {
        AWS_ACCESS_KEY_ID: 'AKSXXXXXXXXXXDFFG',
        AWS_HOST_BASE: 's3.amazonaws.com',
        AWS_SECRET_ACCESS_KEY: 'jIbAXXXXXXXXXXXuM22UY',
        BACKUP_LOCATION: 's3://yb-cloud-backup-dev'
      },
      inUse: false,
      name: 'S3',
      type: 'STORAGE'
    }
  ]
};

beforeEach(() => {
  render(<BackupList activeTab={backupListProps.activeTab} data={backupListProps.data} />);
});

describe('Mutliple Backup config', () => {
  const status = backupListProps.data.map((config) => {
    return config.inUse ? 'Used' : 'Not Used';
  });

  it('Total number of configs', () => {
    expect(screen.getAllByRole('row')).toHaveLength(2);
  });

  it('Config must have action button along with 3 options', () => {
    const actionBtn = screen.getByRole('button', { name: 'Actions' });
    fireEvent.click(actionBtn);

    expect(screen.getAllByRole('menuitem')).toHaveLength(4);

    expect(
      screen.getAllByText(/Edit Configuration/, {
        selector: 'a'
      })
    );

    if (status !== 'Used') {
      expect(
        screen.getAllByText(/Delete Configuration/, {
          selector: 'a'
        })
      );
    }

    expect(
      screen.getAllByText(/Show Universes/, {
        selector: 'a'
      })
    );
  });
});
