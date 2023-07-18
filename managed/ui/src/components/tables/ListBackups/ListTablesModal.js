import { YBModal } from '../../common/forms/fields';

const ListTablesModal = ({ title, visible, data, onHide }) => {
  return (
    <div>
      <YBModal title={title} visible={visible} onHide={onHide} size="small">
        <div className="list-tables-modal__content ">
          {/* eslint-disable-next-line react/jsx-key */}
          {/* eslint-disable-next-line react/display-name */}
          <ul>
            {data?.map((name) => (
              <li>{name}</li>
            ))}
          </ul>
        </div>
      </YBModal>
    </div>
  );
};

export default ListTablesModal;
