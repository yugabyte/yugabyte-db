import { FC, useRef, useState } from 'react';
import { Popover, Row, Col, Button, Overlay } from 'react-bootstrap';
import './UniverseAlertBadge.scss';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { getAlertsCountForUniverse, getAlertsForUniverse } from '../../../actions/customers';
import { YBLoading } from '../../common/indicators';
import { getSeverityLabel } from '../../alerts/AlertList/AlertUtils';
import moment from 'moment';
import { api } from '../../../redesign/helpers/api';
import { toast } from 'react-toastify';

interface AlertBadgeProps {
  universeUUID: string;
  listView?: boolean;
}

interface AlertResponse {
  data: {
    entities: {
      uuid: string;
      name: string;
      createTime: string;
      severity: string;
      state: 'ACTIVE' | 'ACKNOWLEDGED' | 'RESOLVED';
    }[];
  };
}
const AlertPopover = (universeUUID: string, isPopoverShown: boolean, limit: number) => {
  const queryClient = useQueryClient();

  const { data, isLoading } = useQuery(
    ['alert', universeUUID],
    () => getAlertsForUniverse(universeUUID, limit),
    {
      enabled: isPopoverShown && limit !== undefined
    }
  );

  const acknowledge = useMutation((uuid: string) => api.acknowledgeAlert(uuid), {
    onSuccess: () => {
      queryClient.invalidateQueries(['alert', universeUUID]);
      queryClient.invalidateQueries(['alerts', universeUUID, 'count']);
      toast.success('Acknowledged!');
    },
    onError: () => {
      toast.error('Unable to acknowledge. An Error Occured!.');
    }
  });

  const alertList: AlertResponse | undefined = data;
  return (
    <Popover id="alert-popover" onClick={(e) => e.preventDefault()} placement="bottom">
      {isLoading || limit === undefined ? (
        <YBLoading />
      ) : (
        <>
          <h3 className="alert-count">{alertList?.data.entities.length} alerts</h3>
          {/* eslint-disable-next-line react/display-name */}
          {alertList?.data.entities.map((entity) => (
            <Row key={entity.uuid} className="alert-entity">
              <div
                className="alert-name"
                onClick={() => {
                  window.open(`/alerts?showDetails=${entity.uuid}`, '_blank');
                }}
              >
                {entity.name}
              </div>
              <div className="alert-create-time">{moment(entity.createTime).toString()}</div>
              <Row className="alert-labels-actions no-padding">
                <Col lg={6} className="no-padding alerts-labels">
                  <span>{getSeverityLabel(entity.severity)}</span>
                  {entity.state === 'ACTIVE' && <span>{getSeverityLabel('SEVERE', 'Open')}</span>}
                </Col>
                <Col lg={6} className="no-padding">
                  <Button
                    onClick={() => {
                      acknowledge.mutateAsync(entity.uuid);
                    }}
                  >
                    Acknowledge
                  </Button>
                </Col>
              </Row>
            </Row>
          ))}
        </>
      )}
    </Popover>
  );
};
export const UniverseAlertBadge: FC<AlertBadgeProps> = ({ universeUUID, listView }) => {
  const { data: alerts } = useQuery(['alerts', universeUUID, 'count'], () =>
    getAlertsCountForUniverse(universeUUID)
  );
  const iconRef = useRef<any>(undefined);
  const popoverRef = useRef(null);

  const [showPopover, setShowPopover] = useState(false);
  const alertPopover = AlertPopover(universeUUID, showPopover, alerts?.data);
  if (!alerts?.data) {
    return null;
  }
  return (
    <div
      className={`universe-alert-badge ${listView ? 'alert-list-view' : ''}`}
      onClick={(e) => {
        e.preventDefault();
        e.stopPropagation();
      }}
      ref={popoverRef}
    >
      <div ref={iconRef} onClick={() => setShowPopover(!showPopover)} className="icon">
        <i className="fa fa-bell-o alert-bell-icon" />
        <span className="alert-count">{alerts?.data}</span>
      </div>
      <Overlay
        placement="bottom"
        rootClose
        target={iconRef?.current}
        show={showPopover}
        onHide={() => setShowPopover(false)}
        animation={true}
        containerPadding={listView ? 120 : 50}
      >
        {alertPopover}
      </Overlay>
    </div>
  );
};
