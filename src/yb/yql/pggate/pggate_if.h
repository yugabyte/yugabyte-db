#define YBC_CURRENT_CLASS PgApiExample

YBC_CLASS_START
YBC_CONSTRUCTOR(
    ((const char*, database_name))
    ((const char*, table_name))
    ((const char**, column_names))
)
YBC_VIRTUAL_DESTRUCTOR

YBC_VIRTUAL YBC_METHOD_NO_ARGS(bool, HasNext)

YBC_RESULT_METHOD(int32_t, GetInt32Column,
    ((int, column_index))
)

YBC_STATUS_METHOD(GetStringColumn,
    ((int, column_index))
    ((const char**, result))
)

#ifdef YBC_CXX_DECLARATION_MODE
 private:
  PgSession::SharedPtr pg_session_;
  std::string database_name_;
  std::string table_name_;
  std::vector<std::string> columns_;
#endif

YBC_CLASS_END

#undef YBC_CURRENT_CLASS
