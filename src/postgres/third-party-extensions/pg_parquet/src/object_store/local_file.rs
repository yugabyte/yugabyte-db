use std::sync::Arc;

use object_store::local::LocalFileSystem;
use url::Url;

use crate::arrow_parquet::uri_utils::uri_as_string;

use super::object_store_cache::ObjectStoreWithExpiration;

// create_local_file_object_store creates a LocalFileSystem object store with the given path.
pub(crate) fn create_local_file_object_store(
    uri: &Url,
    copy_from: bool,
) -> ObjectStoreWithExpiration {
    let path = uri_as_string(uri);

    if !copy_from {
        // create parent folder if it doesn't exist
        let parent = std::path::Path::new(&path)
            .parent()
            .unwrap_or_else(|| panic!("invalid parent for path: {}", path));

        std::fs::create_dir_all(parent).unwrap_or_else(|e| panic!("{}", e));

        // create or overwrite the local file
        std::fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .create(true)
            .open(path)
            .unwrap_or_else(|e| panic!("{}", e));
    }

    let object_store = LocalFileSystem::new();
    let expire_at = None;

    ObjectStoreWithExpiration {
        object_store: Arc::new(object_store),
        expire_at,
    }
}
