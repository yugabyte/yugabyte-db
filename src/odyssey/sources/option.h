#ifndef OD_OPTION_H
#define OD_OPTION_H

#include <kiwi.h>
#include <argp.h>

extern void od_usage(od_instance_t *instance, char *path);

typedef struct {
	od_instance_t *instance;
	int silent;
	int verbose;
	int console;
	int log_stdout;
} od_arguments_t;

typedef enum {
	OD_OPT_CONSOLE = 10001, // >= than any utf symbol like -q -l etc
	OD_OPT_SILENT,
	OD_OPT_VERBOSE,
	OD_OPT_LOG_STDOUT,
} od_cli_options;

static struct argp_option options[] = {
	{ "verbose", OD_OPT_VERBOSE, 0, OPTION_ARG_OPTIONAL, "Log everything",
	  0 },
	{ "silent", OD_OPT_SILENT, 0, OPTION_ARG_OPTIONAL,
	  "Do not log anything", 0 },
	{ "console", OD_OPT_CONSOLE, 0, OPTION_ARG_OPTIONAL,
	  "Do not fork on startup", 0 },
	{ "log_to_stdout", OD_OPT_LOG_STDOUT, 0, OPTION_ARG_OPTIONAL,
	  "Log to stdout", 0 },
	{ 0 }
};

static inline error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	/* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
	od_arguments_t *arguments = state->input;
	od_instance_t *instance = arguments->instance;

	switch (key) {
	case 'q':
	case 's':
	case OD_OPT_SILENT:
		arguments->silent = 1;
		break;
	case 'v':
	case OD_OPT_VERBOSE:
		arguments->verbose = 1;
		break;
	case 'h': {
		od_usage(instance, instance->exec_path);
	} break;
	case OD_OPT_CONSOLE: {
		arguments->console = 1;
	} break;
	case OD_OPT_LOG_STDOUT: {
		arguments->log_stdout = 1;
	} break;
	case ARGP_KEY_ARG: {
		if (state->arg_num >= 1) {
			/* Too many arguments. */
			od_usage(instance, instance->exec_path);
			return ARGP_KEY_ERROR;
		}

		instance->config_file = strdup(arg);
	} break;
	case ARGP_KEY_END:
		if (state->arg_num < 1) {
			/* Not enough arguments. */
			od_usage(instance, instance->exec_path);
			return ARGP_KEY_ERROR;
		}
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

extern od_retcode_t od_apply_validate_cli_args(od_logger_t *logger,
					       od_config_t *conf,
					       od_arguments_t *args,
					       od_rules_t *rules);

static inline void od_bind_args(struct argp *argp)
{
	/* Program documentation. */
	static char doc[] = "Odyssey - scalable postgresql connection pooler";

	/* A description of the arguments we accept. */
	static char args_doc[] = "/path/to/odyssey.conf";

	memset(argp, 0, sizeof(struct argp));
	argp->options = options;
	argp->parser = parse_opt;
	argp->args_doc = args_doc;
	argp->doc = doc;
}
#endif // OD_OPTION_H
