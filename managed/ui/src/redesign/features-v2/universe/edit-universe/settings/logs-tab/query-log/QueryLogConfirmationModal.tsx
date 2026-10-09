import { FC } from 'react';

import { YBModalProps } from '@app/redesign/components';
import {
  TelemetryConfigConfirmationModal,
  TelemetryConfigConfirmationOperation
} from '../../shared/TelemetryConfigConfirmationModal';
import { QueryLogOperation } from './queryLogHelpers';

interface QueryLogConfirmationModalProps {
  operation: QueryLogOperation;
  universeName: string;
  replicationFactor: number;
  isSubmitting: boolean;
  onSubmit: () => void;
  modalProps: YBModalProps;
}

const mapOperation = (operation: QueryLogOperation): TelemetryConfigConfirmationOperation =>
  operation === 'create' ? 'enable' : 'edit';

export const QueryLogConfirmationModal: FC<QueryLogConfirmationModalProps> = ({
  operation,
  universeName,
  replicationFactor,
  isSubmitting,
  onSubmit,
  modalProps
}) => (
  <TelemetryConfigConfirmationModal
    configType="queryLog"
    operation={mapOperation(operation)}
    universeName={universeName}
    replicationFactor={replicationFactor}
    isSubmitting={isSubmitting}
    onSubmit={onSubmit}
    modalProps={modalProps}
  />
);
