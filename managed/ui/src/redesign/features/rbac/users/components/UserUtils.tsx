import { find } from 'lodash';
import { UniverseResource } from '../../policy/IPolicy';
import { Role } from '../../roles';
import { RbacUser, RbacUserWithResources } from '../interface/Users';
import { Resource } from '../../permission';

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
      role: binding.role,
      roleType: binding.role.roleType,
      resourceGroup: {
        resourceDefinitionSet: binding.resourceGroup.resourceDefinitionSet
      }
    };
    // if the role has universe permission, and the rolebinding doesn't have a universe binding,
    // then add a default binding.
    if (
      find(binding.role.permissionDetails.permissionList, { resourceType: Resource.UNIVERSE }) &&
      !find(resGroup.resourceGroup.resourceDefinitionSet, { resourceType: Resource.UNIVERSE })
    ) {
      resGroup.resourceGroup.resourceDefinitionSet.push({
        allowAll: false,
        resourceType: Resource.UNIVERSE,
        resourceUUIDSet: []
      });
    }
    rbacUserWithResource.roleResourceDefinitions?.push(resGroup);
  });
  return rbacUserWithResource;
};
