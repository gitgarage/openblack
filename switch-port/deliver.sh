#!/usr/bin/env bash
# Installs openblack on the Switch's SD card through the Pixel Tablet's SD relay (payload at "Ready"):
#   sdmc:/switch/openblack/openblack.nro        this build
#   sdmc:/switch/openblack/game/{Data,Audio,Scripts}   the original Black & White data (first time only)
# and fetches the previous run's openblack.log and atlas-crash.log. Waits for the relay to come up.
set -uo pipefail
R=~/IntelliJProjects/atlas/scripts/switch-sd-remote.sh
GAME="${BW_GAME_DIR:-$HOME/old-drive-recovery/taco-bell-comp/Program Files/Lionhead Studios Ltd/Black & White}"
NRO="$(dirname "$0")/build/src/openblack.nro"
LOGS="$(dirname "$0")/logs"; mkdir -p "$LOGS"
for try in $(seq 1200); do
  if nc -z -w2 10.0.1.48 10809 2>/dev/null; then
    sleep 2; $R up >/dev/null 2>&1
    if $R mdir s:/ >/dev/null 2>&1; then
      echo "=== relay up at $(date +%T)"
      $R mcopy -o -n s:/switch/openblack/openblack.log "$LOGS/openblack-prev.log" 2>/dev/null && echo "fetched previous openblack.log"
      $R mcopy -o -n s:/atlas-crash.log "$LOGS/atlas-crash-prev.log" 2>/dev/null && echo "fetched previous atlas-crash.log"
      $R mmd s:/switch/openblack s:/switch/openblack/game 2>/dev/null
      $R mcopy -o -n "$NRO" s:/switch/openblack/openblack.nro && echo "openblack.nro installed ($(stat -c %s "$NRO") bytes)"
      for d in Data Audio Scripts; do
        if $R mdir -b "s:/switch/openblack/game/$d" >/dev/null 2>&1; then
          echo "game $d already on the card"
        else
          start=$(date +%s)
          $R mcopy -s -n "$GAME/$d" s:/switch/openblack/game/ && echo "game $d copied in $(( $(date +%s) - start ))s"
        fi
      done
      $R mdir -/ -b s:/switch/openblack | wc -l | xargs echo "files under /switch/openblack:"
      $R down >/dev/null
      echo "=== done, relay released"
      exit 0
    fi
    $R down >/dev/null 2>&1
  fi
  sleep 3
done
echo "gave up after 60 min"
