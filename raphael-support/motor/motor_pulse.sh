#!/bin/bash
L=/sys/class/leds
state() { grep -E "^ gpio(9|21|33|35|36|130) " /sys/kernel/debug/gpio | sed 's/^ *//' | tr '\n' '|'; }
echo "before: $(state)"
echo 1 > $L/motor:en/brightness
echo 1 > $L/motor:sleep/brightness
sleep 0.05
echo "awake:  $(state)"
# direction A
echo 1 > $L/motor:dir/brightness
echo 1 > $L/motor:m0/brightness
echo 0 > $L/motor:m1/brightness
sleep 0.3
echo 0 > $L/motor:m0/brightness
echo "pulse1: $(state)"
sleep 0.3
# direction B (return)
echo 0 > $L/motor:dir/brightness
echo 0 > $L/motor:m0/brightness
echo 1 > $L/motor:m1/brightness
sleep 0.3
echo 0 > $L/motor:m1/brightness
echo "pulse2: $(state)"
echo 0 > $L/motor:sleep/brightness
echo 0 > $L/motor:en/brightness
echo "after:  $(state)"
