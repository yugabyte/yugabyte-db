SET search_path TO helio_api;


/*
 * Region: RUM operators and functions
 */
 #include "udfs/rum/bson_preconsistent--0.11-0.sql"


/*
 * Region: Collection Metadata
 */
#include "udfs/metadata/collection_metadata_functions--0.11-0.sql"

/*
 * Region: Background Index Schema
 */
 #include "schema/background_index_queue--0.11-0.sql"
 /*
 * Region: Create Index in Background
 */
 #include "udfs/index_mgmt/create_index_background--0.11-0.sql"

 /*
 * Region: Version utils
 */
 #include "udfs/utils/extension_version_utils--0.11-0.sql"

RESET search_path;
