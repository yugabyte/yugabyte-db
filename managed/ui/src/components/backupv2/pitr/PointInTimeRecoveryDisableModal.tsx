/*
 * Created on Wed Jun 08 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { toast } from 'react-toastify';
import { useMutation, useQueryClient } from 'react-query';
import { Col, Row } from 'react-bootstrap';
import { YBModalForm } from '../../common/forms';
import { YBButton } from '../../common/forms/fields';
import { deletePITRConfig } from '../common/PitrAPI';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import './PointInTimeRecoveryDisableModal.scss';

interface PointInTimeRecoveryDisableModalProps {
  visible: boolean;
  universeUUID: string;
  onHide: () => void;
  config: any;
}

export const PointInTimeRecoveryDisableModal: FC<PointInTimeRecoveryDisableModalProps> = ({
  visible,
  onHide,
  config,
  universeUUID
}) => {
  const queryClient = useQueryClient();

  const deletePITR = useMutation((pUUID: string) => deletePITRConfig(universeUUID, pUUID), {
    onSuccess: () => {
      toast.success(`Point-in-time recovery disabled successfully for ${config.dbName}`);
      queryClient.invalidateQueries(['scheduled_sanpshots']);
      onHide();
    },
    onError: (err: any) => {
      toast.error(`Failed to disable point-in-time recovery for ${config.dbName}`);
      onHide();
    }
  });

  const handleSubmit = () => {
    deletePITR.mutateAsync(config.uuid);
  };

  if (!config) return <></>;

  const minTime = config.minRecoverTimeInMillis;
  const retentionDays = config.retentionPeriod / (24 * 60 * 60);

  return (
    <YBModalForm
      title="Disable Point-In-Time Recovery"
      visible={visible}
      onHide={onHide}
      submitLabel="Disable Point-In-Time Recovery"
      onFormSubmit={handleSubmit}
      dialogClassName="pitr-disable-modal"
      submitTestId="DisablePitrSubmitBtn"
      cancelTestId="DisablePitrCancelBtn"
      footerAccessory={<YBButton btnClass="btn" btnText="Cancel" onClick={onHide} />}
      render={() => {
        return (
          <>
            <div className="notice">
              You are about to disable point-in-time recovery for the following database. You will
              no longer be able to perform a point-in-time recovery on this database.
            </div>

            <div className="config-info-c">
              <Row className="config-row">
                <Col sm={6} className="config-row-label">
                  Database/keyspace Name
                </Col>
                <Col className="config-row-val" sm={6}>
                  {config.dbName}
                </Col>
              </Row>

              <Row className="config-row">
                <Col sm={6} className="config-row-label">
                  Retention Period
                </Col>
                <Col sm={6}>
                  {retentionDays} Day{retentionDays > 1 ? 's' : ''}
                </Col>
              </Row>

              <Row className="config-row">
                <Col sm={6} className="config-row-label">
                  Earliest Recoverable Time
                </Col>
                <Col sm={6}>{ybFormatDate(minTime)}</Col>
              </Row>
            </div>
          </>
        );
      }}
    />
  );
};
