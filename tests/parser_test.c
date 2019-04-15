#include "../cfg.h"
#include "test.h/test.h"

TEST_CASE(comments) {
	const char *test_input = "# asdf\n# qwer";
	struct cfg_string_io_handle *sio = cfg_string_io_new(test_input, strlen(test_input));
	struct cfg_comment *ret = parse_comment(&sio->base);
	free(sio);
	free(ret->line);
	free(ret);
}
int main() {}
