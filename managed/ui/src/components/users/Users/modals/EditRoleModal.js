import { Field, Formik } from 'formik';
import { Col, Row } from 'react-bootstrap';
import { YBModal, YBFormSelect } from '../../../common/forms/fields';
import { userRoles } from './AddUserModal';
import { toast } from 'react-toastify';

export const EditRoleModal = ({ modalVisible, onHide, user, changeUserRole, getCustomerUsers }) => {
  if (!user) return null;

  const initialValue = { role: userRoles.find((item) => item.value === user.role) };

  const submitForm = async (data) => {
    const newRole = data.role.value;
    try {
      const resp = await changeUserRole(user.uuid, newRole);
      if (resp.error) {
        toast.error(resp.payload.response.data.error);
      }
      else {
        toast.success("Role changed successfully!");
      }
    } catch (error) {
      console.error('Failed to change user role', error);
    } finally {
      onHide();
      getCustomerUsers();
    }
  };

  return (
    <Formik initialValues={initialValue} onSubmit={submitForm}>
      {({ handleSubmit }) => (
        <YBModal
          visible={modalVisible}
          formName="EditUserRoleForm"
          onHide={onHide}
          onFormSubmit={handleSubmit}
          title={`Edit User Role: ${user.email}`}
          submitLabel="Submit"
          cancelLabel="Close"
          showCancelButton
        >
          <Row>
            <Col lg={3}>
              <div className="form-item-custom-label">Role</div>
            </Col>
            <Col lg={7}>
              <Field
                name="role"
                component={YBFormSelect}
                options={userRoles}
                isSearchable={false}
              />
            </Col>
          </Row>
        </YBModal>
      )}
    </Formik>
  );
};
