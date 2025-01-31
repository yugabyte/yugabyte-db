# src/interfaces/ecpg/preproc/nls.mk
CATALOG_NAME     = ecpg
AVAIL_LANGUAGES  = cs de el es fr it ja ka ko pl pt_BR ru sv tr uk vi zh_CN zh_TW
GETTEXT_FILES    = descriptor.c ecpg.c pgc.c preproc.c type.c variable.c
GETTEXT_TRIGGERS = mmerror:3 mmfatal:2
GETTEXT_FLAGS    = mmerror:3:c-format mmfatal:2:c-format
