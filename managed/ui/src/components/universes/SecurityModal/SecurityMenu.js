import { MenuItem } from 'react-bootstrap';
import { useSelector } from 'react-redux';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBMenuItem } from '../UniverseDetail/compounds/YBMenuItem';

export function SecurityMenu({
  backToMainMenu,
  editTLSAvailability,
  showTLSConfigurationModal,
  showManageKeyModal,
  manageKeyAvailability
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
      <YBMenuItem onClick={showTLSConfigurationModal} availability={tlsAvailability}>
        Encryption in-Transit
      </YBMenuItem>
      <YBMenuItem onClick={showManageKeyModal} availability={manageKeyAvailability}>
        Encryption at-Rest
      </YBMenuItem>
    </>
  );
}
