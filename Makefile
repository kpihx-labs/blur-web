.PHONY: check push

check:
	node --check tools/shot/racedrive.js
	node --check port/harness/web/hcache.js
	node --check port/harness/web/locale.js
	node --check port/harness/web/renderer.js
	node --check port/harness/web/renderer_gpu.js
	node --test port/harness/web/input.test.js
	node --test port/harness/web/locale.test.js
	$(CC) -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pthread -Iport/harness port/harness/test_locale.c -o /tmp/blur-test-locale
	/tmp/blur-test-locale
	$(CC) -std=c11 -Wall -Wextra -Werror -Iport/harness port/harness/test_bink_next.c -o /tmp/blur-test-bink-next
	/tmp/blur-test-bink-next
	uv run python port/harness/test_net_safety.py
	$(CC) -D_GNU_SOURCE -D__EMSCRIPTEN__ -std=c11 -pthread -fsyntax-only port/harness/net.c
	shellcheck tools/shot/racedrive.sh
	uv run python -m py_compile tools/gen_script_bindings.py

push:
	git push github main
