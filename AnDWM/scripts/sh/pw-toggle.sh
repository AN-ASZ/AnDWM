#!/usr/bin/env bash
# pw-toggle.sh — Toggle PipeWire between Low Latency and Audio Quality
# Uses live pw-metadata when audio is active, falls back to state file

STATE_FILE="${XDG_RUNTIME_DIR:-/tmp}/pw-mode"

apply_low_latency() {
    pw-metadata -n settings 0 clock.force-rate 384000
    pw-metadata -n settings 0 clock.force-quantum 4096
    echo "low-latency" > "$STATE_FILE"
    echo "[pw] LOW LATENCY MODE — rate: 384000 Hz, quantum: 4096"
}

apply_audio_quality() {
    pw-metadata -n settings 0 clock.force-rate 44100
    pw-metadata -n settings 0 clock.force-quantum 16
    echo "audio-quality" > "$STATE_FILE"
    echo "[pw] AUDIO QUALITY MODE — rate: 44100 Hz, quantum: 16"
}

get_value() {
    pw-metadata -n settings 2>/dev/null \
        | grep "$1" \
        | grep -oP '(?<="value":")[^"]+' \
        | head -1
}

current_rate="$(get_value clock.force-rate)"
current_quantum="$(get_value clock.force-quantum)"

# If live values are available, use them; otherwise fall back to state file
if [[ -n "$current_rate" || -n "$current_quantum" ]]; then
    if [[ "$current_rate" == "384000" || "$current_quantum" == "4096" ]]; then
        apply_audio_quality
    else
        apply_low_latency
    fi
else
    # No audio running — rely on state file
    current_state="$(cat "$STATE_FILE" 2>/dev/null)"
    if [[ "$current_state" == "low-latency" ]]; then
        apply_audio_quality
    else
        apply_low_latency
    fi
fi