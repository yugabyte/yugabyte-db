use std::sync::Arc;

use object_store::local::LocalFileSystem;
use url::Url;

use super::{object_store_cache::ObjectStoreWithExpiration, uri_as_string};

// create_local_file_object_store creates a LocalFileSystem object store with the given path.
pub(crate) fn create_local_file_object_store(
    uri: &Url,
    copy_from: bool,
) -> ObjectStoreWithExpiration {
    let path = uri_as_string(uri);

    if !copy_from {
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
