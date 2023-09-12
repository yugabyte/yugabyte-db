import { UniverseResource } from '../../policy/IPolicy';
import { Role } from '../../roles';
import { RbacUser, RbacUserWithResources } from '../interface/Users';

export type RbacBindings = {
  createTime: string;
  updateTime: string;
  uuid: string;
  user: RbacUser;
  resourceGroup: {
    resourceDefinitionSet: UniverseResource[];
  };
  role: Role;
  type: Role['roleType'];
};
export const convertRbacBindingsToUISchema = (
  rbacBindings: RbacBindings[]
): RbacUserWithResources => {
  const rbacUserWithResource: RbacUserWithResources = {
    email: rbacBindings[0].user.email,
    roleResourceDefinitions: []
  };
  rbacBindings.forEach((binding) => {
    const resGroup = {
      roleUUID: binding.role.roleUUID,
      resourceGroup: {
        resourceDefinitionSet: binding.resourceGroup.resourceDefinitionSet
      }
    };
    rbacUserWithResource.roleResourceDefinitions?.push(resGroup);
  });
  return rbacUserWithResource;
};
