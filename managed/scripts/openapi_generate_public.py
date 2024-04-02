import yaml
import copy

'''
This script takes the openapi.yaml that contains the complete set of API definitions and generates
a public version of the openapi spec. The public version does not include
"x-yba-api-visibility: internal" parts of the API. This public version of openapi spec is used for
Stoplight documentation.
'''

# Globals
OPENAPI_YML_PATH = "../src/main/resources/openapi.yaml"
OPENAPI_PUBLIC_YML_PATH = "../src/main/resources/openapi_public.yaml"
X_YBA_API_VISIBILITY = "x-yba-api-visibility"
X_YBA_API_VISIBILITY_INTERNAL = "internal"
http_methods = ["get", "post", "put", "patch", "delete"]
global_openapi_dict = {}
global_tag_list = []
global_component_list = []
global_path_list = []


# Loads global openapi.yml file and reads sections
def load_global_file():
    global global_openapi_dict
    global global_tag_list
    global global_component_list
    global global_path_list
    with open(OPENAPI_YML_PATH) as file:
        global_openapi_dict.update(yaml.safe_load(file))
        global_tag_list = copy.deepcopy(global_openapi_dict["tags"])
        global_component_list = copy.deepcopy(global_openapi_dict["components"])
        global_path_list = copy.deepcopy(global_openapi_dict["paths"])


def remove_internal_paths():
    global global_path_list
    path_list = copy.deepcopy(global_path_list)
    paths_to_remove = []
    for path, path_details in path_list.items():
        methods_to_remove = []
        for method, method_details in path_details.items():
            if X_YBA_API_VISIBILITY in method_details \
              and method_details[X_YBA_API_VISIBILITY] == X_YBA_API_VISIBILITY_INTERNAL:
                # collect method to remove
                methods_to_remove.append(method)
        # remove the internal methods
        for method in methods_to_remove:
            path_details.pop(method)
        # remove entire path if no visible methods are remaining
        if not set(path_details.keys()).intersection(set(http_methods)):
            paths_to_remove.append(path)

    for path in paths_to_remove:
        path_list.pop(path)

    # Update the global_openapi_dict with the removed path
    global_openapi_dict["paths"] = path_list
    return path_list


def generate_openapi_public_file():
    global global_openapi_dict

    with open(r'{0}'.format(OPENAPI_PUBLIC_YML_PATH), 'w') as file:
        yaml.dump(global_openapi_dict, file, encoding='utf-8', allow_unicode=True, sort_keys=False)


load_global_file()
remove_internal_paths()
# Do any more processing required here before writing the public openapi spec file
# like removing internal models, properties, etc.

# write the openapi_public.yaml file
generate_openapi_public_file()
print("Generated public openapi successfully at:", OPENAPI_PUBLIC_YML_PATH)
