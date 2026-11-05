.PHONY: check push

check:
	node --check tools/shot/racedrive.js
	shellcheck tools/shot/racedrive.sh
	uv run python -m py_compile tools/gen_script_bindings.py

push:
	git push github main
