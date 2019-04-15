#include "../cfg.h"
#include "test.h/test.h"

#define auto __auto_type

TEST_CASE(string_builder) {
	struct string_builder sb = {0};
	string_builder_push(&sb, "test", 4);
	string_builder_push(&sb, "qwer", 4);
	string_builder_push(&sb, "aaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbccccccccccccccccddddddddddddddddeeeeeeeeeeeeeeee", 80);
	string_builder_push(&sb, "qwer", 4);

	auto ret = string_builder_finish(&sb);
	TEST_EQUAL(strcmp(ret, "testqweraaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbccccccccccccccccddddddddddddddddeeeeeeeeeeeeeeeeqwer"), 0);
	free(ret);

	scoped_string_builder ssb = {0};
	string_builder_push(&ssb, "test", 4);
	string_builder_push(&ssb, "qwer", 4);
}

int main() {}
