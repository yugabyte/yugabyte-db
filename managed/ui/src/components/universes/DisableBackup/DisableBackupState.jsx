import { Col, Row } from 'react-bootstrap';
import { YBModal } from '../../common/forms/fields';

/**
 * recieves current universe details with modal controls.
 * @param {visible, onHide, universe, updateBackupState} props
 * @returns - confirmation modal to toggle backup state
 */
export const ToggleBackupState = (props) => {
  const {
    visible,
    onHide,
    universe: { universeConfig },
    updateBackupState
  } = props;
  const modalTitle = `Disable Backup for: ${props?.universe?.name}?`;

  // toggle backup state for current universe
  const submitBackupState = () => {
    const takeBackups = universeConfig && universeConfig.takeBackups === 'true';
    updateBackupState(props.universe.universeUUID, !takeBackups);
    onHide();
  };
  return (
    <YBModal
      title={modalTitle}
      visible={visible}
      formName="toggleBackupModalForm"
      onHide={onHide}
      showCancelButton={true}
      cancelLabel="Cancel"
      submitLabel="Yes"
      className="universe-action-modal"
      onFormSubmit={submitBackupState}
    >
      <Row>
        <Col lg={12}>Are you sure you want to perform this action?</Col>
      </Row>
    </YBModal>
  );
};
