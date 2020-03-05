





## Get a property of a JSON value

| operator/function | description |
| ---- | ---- |
| `=` | The `=` operator is overloaded for all the SQL data types including `jsonb`. By a strange oversight, there is _no overload_ for plain `json`. |
| `@>` and `<@` | `@>` tests if the left-hand JSON value contains the right-hand JSON value. And `<@` tests if the right-hand JSON value contains the left-hand JSON value. Returns a SQL `boolean`. |
| `?`, `?|`, and `?&` | Test for existence of keys.  Returns a SQL `boolean`. |
| `jsonb_array_length()` | The input must be a JSON _array_. Returns the number of JSON values in the _array_ as a SQL `int`. |
| `jsonb_typeof()` | Takes a single JSON value of arbitrary data type (_string_, _number_, _boolean_, _null_,  _object_, and _array_) and returns the data type name as a SQL `text` value. |
| `jsonb_object_keys()` | Require that the supplied JSON value is an _object_. It transforms the list of key names into a set (i.e. table) of SQL `text` values. |