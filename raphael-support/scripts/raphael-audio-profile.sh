#!/bin/bash
# Make the Raphael sound card usable in WirePlumber/PipeWire.
#
# WirePlumber (ACP backend) keeps the card on the "off" profile until a profile
# is selected, and at boot the ALSA monitor sometimes enumerates before the
# platform sound card is registered.  This helper waits for the card, selects
# the first usable profile (the card only exposes "pro-audio") and makes the
# speaker the default sink.
set -u

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/1000}
WPCTL=$(command -v wpctl) || exit 0

as_user() {
	if [ "$(id -u)" = 0 ]; then
		runuser -u "${RAPHAEL_USER:-user}" -- env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" "$@"
	else
		env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" "$@"
	fi
}

restart_wireplumber() {
	as_user systemctl --user restart --no-block wireplumber 2>/dev/null
}

# Wait for the card to show up in WirePlumber.
card_id=""
for i in $(seq 1 30); do
	card_id=$(as_user timeout 5 "$WPCTL" status 2>/dev/null | awk '/alsa/ {print $2; exit}' | tr -d '.')
	[ -n "$card_id" ] && break
	if [ "$i" = 10 ] || [ "$i" = 20 ]; then
		restart_wireplumber
	fi
	sleep 2
done

if [ -z "$card_id" ]; then
	echo "raphael-audio: sound card never appeared in WirePlumber"
	exit 0
fi

# Pick the first profile that is not "off".
profile=$(as_user timeout 5 "$WPCTL" inspect "$card_id" 2>/dev/null | sed -n 's/.*profiles:.*//p' )
profile=$(as_user timeout 5 pw-dump "$card_id" 2>/dev/null | python3 -c '
import json,sys
try:
    dev=json.load(sys.stdin)[0]
except Exception:
    sys.exit(0)
params=dev["info"]["params"]
for p in params.get("EnumProfile", []):
    if p.get("name") != "off" and p.get("available") == "yes":
        print(p["index"])
        break
' 2>/dev/null)

if [ -n "${profile:-}" ]; then
	as_user timeout 5 "$WPCTL" set-profile "$card_id" "$profile" 2>/dev/null
	echo "raphael-audio: card $card_id -> profile $profile"
fi

sleep 2

# Speaker first: it is the sink whose node name ends with "Pro 1"
# (PCM device 1 = MultiMedia2 = QUAT_MI2S -> TFA9874).
speaker=$(as_user timeout 5 pw-dump 2>/dev/null | python3 -c '
import json,sys
try:
    objs=json.load(sys.stdin)
except Exception:
    sys.exit(0)
for o in objs:
    if o.get("type") != "PipeWire:Interface:Node":
        continue
    p=o["info"]["props"]
    if p.get("media.class") != "Audio/Sink":
        continue
    name=p.get("node.name","")
    desc=p.get("node.description","")
    if "Pro 1" in desc or name.endswith("pro-1") or name.endswith("pro_1"):
        print(o["id"])
        break
' 2>/dev/null)

if [ -n "${speaker:-}" ]; then
	as_user timeout 5 "$WPCTL" set-default "$speaker" 2>/dev/null
	echo "raphael-audio: default sink $speaker (speaker)"
fi

exit 0
