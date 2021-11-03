#include "unit.h"
#include "uri/uri.h"
#include "lua/utils.h"
#include "trivia/util.h"
#include "diag.h"
#include "memory.h"
#include "fiber.h"
#include "tt_static.h"

#include <stdio.h>

#define URI_PARAM_MAX 10
#define URI_PARAM_VALUE_MAX 10

struct uri_param_expected {
	/** URI parameter name */
	const char *name;
	/** Count of URI parameter values */
	int value_count;
	/** Expected URI parameter values */
	const char *values[URI_PARAM_VALUE_MAX];
};

struct uri_expected {
	/** String URI passed for parse and validation */
	const char *string;
	/** Count of URI parameters */
	int param_count;
	/** Array of expected URI parameters */
	struct uri_param_expected params[URI_PARAM_MAX];
};

static int
uri_param_expected_check(const struct uri_param_expected *param,
			       const struct uri *uri)
{
	plan(1 + param->value_count);
	int value_count = uri_param_count(uri, param->name);
	is(param->value_count, value_count, "value count");
	for (int idx = 0; idx < MIN(value_count, param->value_count); idx++) {
		const char *value = uri_param(uri, param->name, idx);
		is(strcmp(value, param->values[idx]), 0, "param value");
	}
	return check_plan();
}

static int
uri_expected_check(const struct uri_expected *uri_ex, const struct uri *uri)
{
	plan(1 + uri_ex->param_count);
	is(uri_ex->param_count, uri->param_count, "param count");
	for (int i = 0; i < MIN(uri_ex->param_count, uri->param_count); i++)
		uri_param_expected_check(&uri_ex->params[i], uri);
	return check_plan();
}

static int
test_string_uri_with_query_params_parse(void)
{
	const struct uri_expected uris[] = {
		/* One string URI without parameters. */
		[0] = {
			.string = "/unix.sock",
			.param_count = 0,
			.params = {},
		},
		/* One string URI without parameters with additional '?'. */
		[1] = {
			.string = "/unix.sock?",
			.param_count = 0,
			.params = {},
		},
		/* One string URI with one parameter and one parameter value. */
		[2] = {
			.string = "/unix.sock?q1=v1",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * Same as previous but with extra '&' at the end
		 * of the string.
		 */
		[3] = {
			.string = "/unix.sock?q1=v1&",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * Same as previos but with two extra '&' at the end
		 * of the string.
		 */
		[4] = {
			.string = "/unix.sock?q1=v1&&",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "v1" },
				},
			},
		},
		/*
		 * One string URI with one parameter and two parameter values,
		 * separated by "&".
		 */
		[5] = {
			.string = "/unix.sock?q1=v1&q1=v2",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "v2" },
				},
			},
		},
		/*
		 * Same as previous but with extra '&' between parameters.
		 */
		[6] = {
			.string = "/unix.sock?q1=v1&&q1=v2",
			.param_count = 1,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "v2" },
				},
			},
		},
		/*
		 * On string uri with several parameters without values.
		 */
		[7] = {
			.string = "/unix.sock?q1&q2",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 0,
					.values = {},
				},
				[1] = {
					.name = "q2",
					.value_count = 0,
					.values = {},
				},
			}
		},
		/*
		 * One string URI with several parameters.
		 */
		[8] = {
			.string = "/unix.sock?q1=v11&q1=v12&q2=v21&q2=v22",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v11", "v12" },
				},
				[1] = {
					.name = "q2",
					.value_count = 2,
					.values = { "v21", "v22" },
				},
			},
		},
		/*
		 * One string URI with several parameters, at the same time,
		 * some of them have empty value or don't have values at all.
		 */
		[9] = {
			.string = "/unix.sock?q1=v1&q1=&q2&q3=",
			.param_count = 3,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 2,
					.values = { "v1", "" },
				},
				[1] = {
					.name = "q2",
					.value_count = 0,
					.values = {},
				},
				[2] = {
					.name = "q3",
					.value_count = 1,
					.values = { "" },
				},
			},
		},
		/*
		 * Single URI with query, that contains extra '=' between
		 * parameter and it's value. (All extra '=' is interpreted
		 * as a part of value).
		 */
		[10] = {
			.string = "/unix.sock?q1===v1&q2===v2",
			.param_count = 2,
			.params = {
				[0] = {
					.name = "q1",
					.value_count = 1,
					.values = { "==v1" },
				},
				[1] = {
					.name = "q2",
					.value_count = 1,
					.values = { "==v2" },
				},
			},
		},
		/*
		 * Single URI with strange query, that contains combination
		 * of delimiters.
		 */
		[11] = {
			.string = "/unix.sock?&=&=",
			.param_count = 0,
			.params = {},
		},
		/*
		 * Same as previous, but another sequence of delimiters.
		 */
		[12] = {
			.string = "/unix.sock?=&=&",
			.param_count = 0,
			.params = {},
		}
	};
	plan(2 * lengthof(uris));
	struct uri u;
	for (unsigned i = 0; i < lengthof(uris); i++) {
		int rc = uri_create(&u, uris[i].string);
		is(rc, 0, "%s: parse", uris[i].string);
		uri_expected_check(&uris[i], &u);
		uri_destroy(&u);
	}
	return check_plan();
}

int
main(void)
{
	plan(1);
	test_string_uri_with_query_params_parse();
	return check_plan();
}
