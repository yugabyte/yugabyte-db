import { MenuItem } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBMenuItem } from '../UniverseDetail/compounds/YBMenuItem';
import { isActionFrozen } from '../../../redesign/helpers/utils';
import { UNIVERSE_TASKS } from '../../../redesign/helpers/constants';

export function SecurityMenu({
  backToMainMenu,
  editTLSAvailability,
  showTLSConfigurationModal,
  showManageKeyModal,
  manageKeyAvailability,
  allowedTasks
}) {
  const { test, released } = useSelector((state) => state.featureFlags);

  const tlsAvailability =
    editTLSAvailability === 'enabled' ||
    test.enableNewEncryptionInTransitModal ||
    released.enableNewEncryptionInTransitModal
      ? 'enabled'
      : 'disabled';

  return (
    <>
      <MenuItem onClick={backToMainMenu}>
        <YBLabelWithIcon icon="fa fa-chevron-left fa-fw">Back</YBLabelWithIcon>
      </MenuItem>
      <MenuItem divider />
      <YBMenuItem
        onClick={showTLSConfigurationModal}
        availability={tlsAvailability}
        disabled={isActionFrozen(allowedTasks, UNIVERSE_TASKS.ENCRYPTION_IN_TRANSIT)}
      >
        Encryption in-Transit
      </YBMenuItem>
      <YBMenuItem
        onClick={showManageKeyModal}
        availability={manageKeyAvailability}
        disabled={isActionFrozen(allowedTasks, UNIVERSE_TASKS.ENCRYPTION_AT_REST)}
      >
        Encryption at-Rest
      </YBMenuItem>
    </>
  );
}
