.PHONY: check push

check:
	node --check tools/shot/racedrive.js
	shellcheck tools/shot/racedrive.sh

push:
	git push github main
