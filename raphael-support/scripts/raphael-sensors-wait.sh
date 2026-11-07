#!/bin/sh
# Wait until the SDSP sensor service answers, so that iio-sensor-proxy can
# discover all SSC sensors in one go (its discovery is not retried).
i=0
while [ $i -lt 30 ]; do
	if ssccli --sensor accelerometer --timeout 1 >/dev/null 2>&1; then
		exit 0
	fi
	i=$((i + 1))
	sleep 1
done
# Not fatal: let the proxy start anyway, light/proximity may still be usable
exit 0
