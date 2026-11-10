#!/bin/bash
L=/sys/class/leds
hall() { grep -E "^ gpio27 " /sys/kernel/debug/gpio | head -1 | awk '{print $4}'; }
pins() { echo "m0=$(cat $L/motor:m0/brightness) m1=$(cat $L/motor:m1/brightness) dir=$(cat $L/motor:dir/brightness) sleep=$(cat $L/motor:sleep/brightness) en=$(cat $L/motor:en/brightness)"; }
echo "hall(before)=$(hall)"
echo 1 > $L/motor:en/brightness
echo 1 > $L/motor:sleep/brightness
sleep 0.05
# forward
echo 1 > $L/motor:dir/brightness
echo 1 > $L/motor:m0/brightness
echo 0 > $L/motor:m1/brightness
echo "driving A: $(pins) hall=$(hall)"
sleep 1.2
echo 0 > $L/motor:m0/brightness
echo "stopped A: hall=$(hall)"
sleep 0.5
# reverse
echo 0 > $L/motor:dir/brightness
echo 0 > $L/motor:m0/brightness
echo 1 > $L/motor:m1/brightness
echo "driving B: $(pins) hall=$(hall)"
sleep 1.2
echo 0 > $L/motor:m1/brightness
echo "stopped B: hall=$(hall)"
echo 0 > $L/motor:sleep/brightness
echo 0 > $L/motor:en/brightness
echo "hall(final)=$(hall)"
