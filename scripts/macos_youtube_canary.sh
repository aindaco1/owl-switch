#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <sources|playback> <absolute-app> <absolute-helper-home>" >&2
    exit 64
fi

mode="$1"
app="$2"
helper_home="$3"
if [[ "$mode" != "sources" && "$mode" != "playback" ]] || \
   [[ "$app" != /* || "$helper_home" != /* || ! -d "$app/Contents/Resources/bin" || -L "$app" ]]; then
    echo "invalid YouTube canary input" >&2
    exit 64
fi

bin="$app/Contents/Resources/bin"
mkdir -p "$helper_home"

if [[ "$mode" == "sources" ]]; then
    channel_ids=(
        UCtPzvwooQ18YZ8Wq8Hka60g UCBfV298JqKc8o9CM0aANz5A
        UCoVB6wMm2pNGMKQTChKCiRQ UCIauImhx1GGrl7LubRCxXcg
        UCNU7LlZ_nKVaq9Lihj0sAHQ UCR0kPElUivbuZC7Myr7Tg1Q
        UCTQHT1Gj_D_Bc7P1REuMoAg UCPZsA3OSQreeZlKIo6jqUog
        UCvgYvYeZe-BANj-cVUd59mQ UCkXE7x417ME2iudNECaLUFA
        UC6m4V2RfKXs4dP3R7AfCK4g UCqspiYXbxpZpgWzzxUUbTiw
        UCdwO61VZMYpozDiAJ6ZI3pg UC4T6FfTdpvxUrf9-dd4kjpw
        UCxuk5azVGJ-aumAds7WMHmg UCg0i5aSL_2rhf4iztlLmLUQ
        UCM8kkIU5aIzCbyZawksZ2Bw UCbqcG1rdt9LMwOJN4PyGTKg
    )
    for channel_id in "${channel_ids[@]}"; do
        sample="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin \
            "$bin/yt-dlp" --no-config --no-update --no-cache-dir --no-warnings \
            --no-progress --flat-playlist --lazy-playlist --playlist-end 1 \
            --js-runtimes "deno:$bin/deno" \
            --print '%(id)s\t%(title)s' \
            "https://www.youtube.com/channel/$channel_id/videos")"
        [[ -n "$sample" ]]
    done

    local_playlist_sample="$(env -i HOME="$helper_home" PATH=/usr/bin:/bin \
        "$bin/yt-dlp" --no-config --no-update --no-cache-dir --no-warnings \
        --no-progress --ignore-errors --flat-playlist --lazy-playlist \
        --dump-json --playlist-end 1 \
        --js-runtimes "deno:$bin/deno" \
        'https://www.youtube.com/playlist?list=PLt5yu3-wZAlSLRHmI1qNm0wjyVNWw1pCU' \
        | /usr/bin/python3 -c \
          'import json,sys; print(json.loads(sys.stdin.readline()).get("id", ""))')"
    [[ "$local_playlist_sample" == "aqz-KE-bpKQ" ]]
    echo "YouTube source canaries passed (${#channel_ids[@]} Karaoke sources and Local playlist import)"
    exit 0
fi

common_mpv_options=(
    --ao=null --length=0.25 --no-config --no-terminal
    --msg-level=ytdl_hook=debug,ffmpeg=warn
    "--script-opts-append=ytdl_hook-ytdl_path=$bin/yt-dlp"
    --ytdl-raw-options-append=ignore-config=
    --ytdl-raw-options-append=no-update=
    --ytdl-raw-options-append=no-cache-dir=
    --ytdl-raw-options-append=check-formats=
    "--ytdl-raw-options-append=js-runtimes=deno:$bin/deno"
)
canary_url="https://www.youtube.com/watch?v=aqz-KE-bpKQ"
env -i HOME="$helper_home" PATH=/usr/bin:/bin \
    "$bin/mpv" "$canary_url" --no-video --ytdl-format=bestaudio/best \
    "${common_mpv_options[@]}"
env -i HOME="$helper_home" PATH=/usr/bin:/bin \
    "$bin/mpv" "$canary_url" --vo=null \
    '--ytdl-format=bestvideo[ext=mp4][height<=720]+bestaudio[ext=m4a]/best[ext=mp4][height<=720]/best[height<=720]/best' \
    "${common_mpv_options[@]}"
echo "YouTube audio and 720p video playback canaries passed"
