#!/bin/bash
L=/sys/class/leds
hall() { grep -E "^ gpio27 " /sys/kernel/debug/gpio | head -1 | awk '{print $4}'; }
setp() { echo $2 > $L/motor:$1/brightness; }
trial() { # dir m0 m1 ms label
  setp en 1; setp sleep 1; sleep 0.05
  setp dir $1; setp m0 $2; setp m1 $3
  sleep $4
  setp m0 0; setp m1 0
  echo "$5: hall=$(hall)"
  sleep 0.4
}
echo "hall(start)=$(hall)"
trial 1 1 0 0.8 "dir1 m0=1 m1=0"
trial 0 1 0 0.8 "dir0 m0=1 m1=0"
trial 1 0 1 0.8 "dir1 m0=0 m1=1"
trial 0 0 1 0.8 "dir0 m0=0 m1=1"
trial 1 1 0 2.0 "dir1 m0=1 m1=0 (long)"
trial 0 0 1 2.0 "dir0 m0=0 m1=1 (long)"
setp sleep 0; setp en 0
echo "hall(end)=$(hall)"
