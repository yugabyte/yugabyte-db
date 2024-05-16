import yaml
import copy
import logging

'''
This script takes the openapi.yaml that contains the complete set of API definitions and generates
openapi_public.yaml spec. The public version does not include "x-yba-api-visibility: internal"
parts of the API.

For parts of the API marked with "x-yba-api-visibility: preview" this script ensures:
1. there is also a corresponding "x-yba-api-since: <yba-version>" at the same level
2. generates the message "WARNING: This is a preview API that could change" in the description

For parts of the API marked with "x-yba-api-visibility: deprecated" this script ensures:
1. there is also a corresponding "x-yba-api-since: <yba-version>" at the same level
2. generates the message "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.</b>" in
   the description
3. generates "deprecated : true" if this marker is on a path

TODO: Also handle validation of date-time type is of RFC3339 format.

This openapi_public.yaml is used for Stoplight documentation.
'''

# Globals
OPENAPI_YML_PATH = "../src/main/resources/openapi.yaml"
OPENAPI_PUBLIC_YML_PATH = "../src/main/resources/openapi_public.yaml"
X_YBA_API_VISIBILITY = "x-yba-api-visibility"
X_YBA_API_VISIBILITY_INTERNAL = "internal"
X_YBA_API_VISIBILITY_DEPRECATED = "deprecated"
X_YBA_API_VISIBILITY_PREVIEW = "preview"
X_YBA_API_SINCE = "x-yba-api-since"
DEPRECATED_MSG_FMT = "<b style=\"color:#ff0000\">Deprecated since YBA version {}.</b></p>"
PREVIEW_MSG_FMT = ("<b style=\"color:#FFA500\">WARNING: This is a preview API in YBA version {}"
                   " that could change.</b></p>")
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


# Remove any paths marked with x-yba-api-visibility: "internal"
def remove_internal_paths():
    global global_path_list
    paths_to_remove = []
    for path, path_details in global_path_list.items():
        methods_to_remove = []
        for method, method_details in path_details.items():
            if is_internal(method_details):
                # collect method to remove
                methods_to_remove.append(method)
        # remove the internal methods
        for method in methods_to_remove:
            path_details.pop(method)
            logger.debug("Removed internal path " + method + " " + path)
        # remove entire path if no visible methods are remaining
        if not set(path_details.keys()).intersection(set(http_methods)):
            paths_to_remove.append(path)

    for path in paths_to_remove:
        global_path_list.pop(path)

    # Update the global_openapi_dict with the removed path
    global_openapi_dict["paths"] = global_path_list


# Remove schemas and properties marked with x-yba-api-visibility: "internal"
def remove_internal_schemas_and_properties():
    global global_component_list
    schemas_to_remove = []
    for schema_name, schema in global_component_list["schemas"].items():
        if is_internal(schema):
            schemas_to_remove.append(schema_name)
            continue
        props_to_remove = []
        if "properties" in schema:
            for prop_name, prop in schema["properties"].items():
                if is_internal(prop):
                    props_to_remove.append(prop_name)
        # remove internal properties
        for prop_name in props_to_remove:
            schema["properties"].pop(prop_name)
            logger.debug("Removed internal property " + prop_name + " from " + schema_name)
        # remove entire schema if no visible properties are remaining
        if "properties" in schema and schema["properties"] is None:
            schemas_to_remove.append(schema_name)
            logger.debug("Removed internal schema " + schema_name)

    # remove internal schemas
    for schema_name in schemas_to_remove:
        global_component_list["schemas"].pop(schema_name)
        logger.debug("Removed internal schema " + schema_name)
    # Update the global_openapi_dict with the removed components
    global_openapi_dict["components"] = global_component_list


# For paths marked with "x-yba-api-visibility: deprecated" or "preview" this method ensures:
# 1. there is also a corresponding "x-yba-api-since: <yba-version>" at the same level
# 2. generates the deprecation message "<b style=\"color:#ff0000\">Deprecated since YBA version
#    2.20.0.</b>" in the description of deprecated paths.
# 3. generates the preview message "WARNING: This is a preview API in YBA version 2.20.0.0 that
#    could change." in the description of preview paths.
# 4. generates "deprecated : true" for this path for deprecated paths.
def process_visibility_in_paths():
    global global_path_list
    paths_with_errors = []
    for path, path_details in global_path_list.items():
        for method, method_details in path_details.items():
            for visibility in [X_YBA_API_VISIBILITY_PREVIEW, X_YBA_API_VISIBILITY_DEPRECATED]:
                if has_visibility(method_details, visibility):
                    if X_YBA_API_SINCE not in method_details:
                        paths_with_errors.append((path, method, method_details))
                        continue
                    # add visibility message to description
                    add_visibility_desc(method_details, visibility)
    # raise error for paths missing X_YBA_API_SINCE
    if paths_with_errors:
        errMsgs = []
        for (path, method, _) in paths_with_errors:
            errMsgs.append(X_YBA_API_SINCE + ": is missing in " + method + " method of " + path)
        raise Exception(", ".join(errMsgs))
    # Update the global_openapi_dict with the updated paths
    global_openapi_dict["paths"] = global_path_list


# For schema and properties marked with "x-yba-api-visibility: deprecated" this method ensures:
# 1. there is also a corresponding "x-yba-api-since: <yba-version>" at the same level
# 2. generates the message "<b style=\"color:#ff0000\">Deprecated since YBA version 2.20.0.</b>" in
#    the description
def process_visibility_in_schemas_and_properties():
    global global_component_list
    schemas_with_errors = []
    props_with_errors = []
    for schema_name, schema in global_component_list["schemas"].items():
        for visibility in [X_YBA_API_VISIBILITY_PREVIEW, X_YBA_API_VISIBILITY_DEPRECATED]:
            if has_visibility(schema, visibility):
                if X_YBA_API_SINCE not in schema:
                    schemas_with_errors.append(schema_name)
                    continue
                add_visibility_desc(schema, visibility)

            # process properties
            if "properties" in schema:
                for prop_name, prop in schema["properties"].items():
                    if has_visibility(prop, visibility):
                        if X_YBA_API_SINCE not in prop:
                            props_with_errors.append((prop_name, schema_name))
                            continue
                        add_visibility_desc(prop, visibility)

            global_component_list["schemas"][schema_name] = schema

    # raise error for schemas missing X_YBA_API_SINCE
    if schemas_with_errors or props_with_errors:
        errMsgs = []
        for schema_name in schemas_with_errors:
            errMsgs.append(X_YBA_API_SINCE + ": is missing in schema " + schema_name)
        for (prop_name, schema_name) in props_with_errors:
            errMsgs.append(X_YBA_API_SINCE + ": is missing in property '" + prop_name
                           + "' of schema " + schema_name)
        raise Exception(", ".join(errMsgs))

    # Update the global_openapi_dict with the removed components
    global_openapi_dict["components"] = global_component_list


def generate_openapi_public_file():
    global global_openapi_dict

    with open(r'{0}'.format(OPENAPI_PUBLIC_YML_PATH), 'w') as file:
        yaml.dump(global_openapi_dict, file, encoding='utf-8', allow_unicode=True, sort_keys=False)


# checks if this object has x-yba-api-visibility set to given visiblity
def has_visibility(obj, visibility):
    return X_YBA_API_VISIBILITY in obj and obj[X_YBA_API_VISIBILITY] == visibility


# adds a deprecated or preview message to given obj's description
def add_visibility_desc(obj, visibility):
    # TODO: Validate that x-yba-api-since is set to a valid yba version string
    msg = ""
    if visibility == X_YBA_API_VISIBILITY_DEPRECATED:
        msg = DEPRECATED_MSG_FMT.format(obj[X_YBA_API_SINCE])
    elif visibility == X_YBA_API_VISIBILITY_PREVIEW:
        msg = PREVIEW_MSG_FMT.format(obj[X_YBA_API_SINCE])
    if msg not in obj["description"]:
        obj["description"] = msg + obj["description"]
    if visibility == X_YBA_API_VISIBILITY_DEPRECATED:
        # add deprecated: true
        obj["deprecated"] = True


def is_deprecated(obj):
    return has_visibility(obj, X_YBA_API_VISIBILITY_DEPRECATED)


def is_preview(obj):
    return has_visibility(obj, X_YBA_API_VISIBILITY_PREVIEW)


def is_internal(obj):
    return has_visibility(obj, X_YBA_API_VISIBILITY_INTERNAL)


logging.basicConfig(format='%(asctime)s [%(filename)s:%(lineno)d] %(message)s',
                    datefmt='%Y-%m-%d:%H:%M:%S',
                    level=logging.DEBUG)
logger = logging.getLogger(__name__)
load_global_file()
logger.info("Removing paths that are marked 'x-yba-api-vibility: internal'")
remove_internal_paths()
logger.info("Removing schemas and properties that are marked 'x-yba-api-vibility: internal'")
remove_internal_schemas_and_properties()
logger.info("Processing paths that are marked 'x-yba-api-vibility: deprecated' or 'preview'")
process_visibility_in_paths()
logger.info(("Processing schemas and properties that are marked 'x-yba-api-vibility: deprecated'"
             " or 'preview'"))
process_visibility_in_schemas_and_properties()

# write the openapi_public.yaml file
generate_openapi_public_file()
logger.info("Generated public openapi successfully at: " + OPENAPI_PUBLIC_YML_PATH)
