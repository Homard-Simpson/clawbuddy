.PHONY: status live profiles test serve

status:
	bin/clawbuddy status

live:
	bin/clawbuddy status --live

profiles:
	bin/clawbuddy profiles

serve:
	bin/clawbuddy-server

test:
	python3 -m py_compile bin/clawbuddy bin/clawbuddy-server
	rm -rf bin/__pycache__
	bin/clawbuddy profiles >/tmp/clawbuddy-profiles.txt
	bin/clawbuddy status --json | python3 -m json.tool >/tmp/clawbuddy-status.json
	bin/clawbuddy status --live --json | python3 -m json.tool >/tmp/clawbuddy-live-status.json
	bin/clawbuddy vision list --no-scan --json | python3 -m json.tool >/tmp/clawbuddy-vision-cameras.json
	bin/clawbuddy vision scene-prompt >/tmp/clawbuddy-vision-policy.txt
