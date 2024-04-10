# inline-thirdparty

This is a directory where we copy some of the third-party header-only libraries, rather than adding
them to the yugabyte-db-thirdparty repo. We also only copy the relevant subdirectory of the upstream
repositories. Each library is copied in its own appropriately named directory, and each library's
directory is added separately to the list of include directories in CMakeLists.txt.

* usearch
  * Repo: https://github.com/yugabyte/usearch
  * Description: Similarity search for vector and text
  * Subdirectory: include
  * Tag: v2.11.0-yb-1
  * License: Apache 2.0

* fp16
  * Repo: https://github.com/Maratyszcza/FP16/
  * Description: Header-only library for conversion to/from half-precision floating point formats
  * Subdirectory: include
  * Commit: 0a92994d729ff76a58f692d3028ca1b64b145d91
  * License: MIT
