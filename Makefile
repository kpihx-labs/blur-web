.PHONY: check push

check:
	node --check tools/shot/racedrive.js
	node --test port/harness/web/input.test.js
	uv run python port/harness/test_net_safety.py
	$(CC) -D_GNU_SOURCE -D__EMSCRIPTEN__ -std=c11 -pthread -fsyntax-only port/harness/net.c
	shellcheck tools/shot/racedrive.sh
	uv run python -m py_compile tools/gen_script_bindings.py

push:
	git push github main
