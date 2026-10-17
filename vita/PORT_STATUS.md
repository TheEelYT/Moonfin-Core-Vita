# Vita port status

Version: **0.1.2 native preview**. This is an initial native implementation, not a declaration of full Moonfin parity. No physical Vita or personal Jellyfin/Emby server is attached to the development environment.

## Implemented paths

These paths have source implementations. Hardware playback, system UI, performance and real-server compatibility still require device validation.

| Area | Native implementation |
| --- | --- |
| Installation | VitaSDK build, VPK, independent title ID and Moonfin artwork |
| Server setup | Manual URL, HTTP/verified HTTPS, reverse-proxy base paths |
| Authentication | Jellyfin/Emby password sign-in, Jellyfin Quick Connect, saved accounts, account removal |
| Multiple servers | Switch between saved accounts; libraries are not merged |
| Home | Continue watching, next up, latest items from up to three libraries |
| Library | Poster grid, paginated folders, series/seasons/episodes, movies and music libraries |
| Search | Search movies, series, episodes, albums, artists and audio |
| Details | Synopsis, runtime, year, rating, cast, versions, favorites and watched state |
| Playback | Vita mpv GXM framebuffer, hardware H.264 path, server-negotiated direct play/transcoding |
| Playback controls | Pause, seeks, volume, track/quality switching, resume selection |
| Subtitles | Server-rendered subtitles selected through PlaybackInfo |
| Resume | Start/progress/stop session reports, 10-second progress interval, transcode cleanup |
| TV flow | Next-episode action after natural episode completion |
| Segments | Explicit skip action for server-provided media segments |
| Music | Audio item playback through mpv; browsing albums/artists |
| Live TV | Channel/current-program list and recordings; negotiated playback |
| Seerr | Search and request movies/all seasons via Moonbase's authenticated proxy |
| Resources | Lazy artwork fetches, 30-item library pages, bounded HTTP and decoder caches |
| Lifecycle | Main-thread GXM calls; pause/resume on Vita focus changes; exit cleanup |

## Still needed for the full port

- Device testing and correction of startup, keyboard, touch layout, audio/video, seek and suspend behavior.
- Match Moonfin's featured bar, navigation, detail styles, themes and animations on the 960×544 screen.
- Server discovery, richer sign-in/user selection, Emby Connect, and merged multi-server browsing.
- Complete music queues, autoplay/shuffle/repeat, mini player, lyrics and audiobook chapter/bookmark features.
- Offline downloads and automatic episode downloads.
- Full EPG/DVR scheduling and recording management; larger live-channel pagination.
- Trickplay thumbnails, chapter navigation, automatic intro/credits skipping and cinema mode.
- Moonbase settings/theme synchronization, notifications, richer ratings and discovery rows.
- Casting, remote control and SyncPlay.
- Kids Mode/PIN, achievements, admin tools and the remaining advanced preferences.
- Ebooks/comics and retro-game integration.
- Native subtitle rendering/styling and support beyond the conservative playback profile, if feasible.
- Testing of server transcoding failures, denied permissions, network loss, expired sessions and long-running memory use on hardware.

Features that cannot map to the Vita's display, codec hardware or OS will need explicit adaptations. Full desktop codec output, HDR and audio passthrough are not promised by this native preview.

## Verification

Portable validation: 76 protocol/local HTTP checks and 11 regression tests passed. The Vita workflow cross-builds the VPK and checks its identity, assets and linked pthread stack override. The user confirmed that the corrected VPK installs, launches and signs in to their server. Playback validation is pending.

The first hardware installation attempt reported VitaShell error `0x8010113D`. The original package used truecolor LiveArea PNGs, including an RGBA icon. The corrected package uses opaque 8-bit indexed PNGs for the icon, background and startup image. Package validation now checks their encoding and dimensions, with regression tests rejecting truecolor, alpha and incorrect sizes. The user confirmed that the corrected package installs and launches. Startup then stopped at `ytdl: option not found`: the pinned Vita mpv build disables Lua and therefore omits that option. The unnecessary `ytdl=no` setting has been removed; the user subsequently confirmed successful startup and sign-in.

The portable tests exercise the code used by the native client: URL escaping/base paths, login, profile limits, direct/transcoded resolution, track indices, resume tick conversion, session cleanup, settings persistence, queue ordering, HTTP request transport, redirect refusal and response-size bounds. They use scripted responses and a local HTTP test server. They do **not** establish that a video has played on a real Vita.

## 0.1.1 main-menu crash correction

The uploaded core dump from build `6f1b79f` stops at `0x810b2320` in `brls::Application::handleAction`. Its current-focus pointer references freed login UI memory, already reused for JSON data. The pinned Borealis implementation ignored `giveFocus(nullptr)`; replacing the login screen left that dangling pointer active.

The Vita build now compiles a generated Borealis override that releases null focus and clears focus before activity teardown. The submodule remains unchanged. New menu buttons take focus when their active page has none. Menu callbacks wait until action dispatch has finished and are cancelled if the button is deleted first, covering refresh, server switching and page retries. Host regressions execute the actual dependency focus methods and menu callback class against lightweight UI/queue stubs: the original focus methods fail, and the patched methods pass. These checks do not emulate drawing or establish device stability.

Build artifacts now include source-line information and the original ELF for future dump analysis. The user progressed to a playback attempt on 0.1.1; long-running navigation and server-switching checks remain useful.

## 0.1.2 playback startup and resolution policy

The next device dump faults at `0x816da122` in Vita newlib `poll`, called by FFmpeg `ff_connect_parallel`/TCP/HTTP. The crashed pthread has a 32 KiB stack starting at `0x81bb9000`; its stack pointer is `0x81bb8cd8`, below that allocation. The faulting instruction stores a local variable after `poll` reserves about 9 KiB. This is stack exhaustion during stream connection, before decoding.

The app now supplies the Vita pthread override `_pthread_stack_default_user = 2 MiB`, also used by the referenced Switchfin Vita target. Package validation checks the final ELF for a strong, defined override with the intended allocation; the original 0.1.1 ELF fails this check. On-device playback confirmation is pending.

The maximum video dimensions are now 960×544, including when legacy settings request 720p. The lower preset is 640×360. Known oversized sources disable direct play immediately. If dimensions only arrive in PlaybackInfo, an oversized or unknown-size source incorrectly marked directly playable is renegotiated with direct play disabled and its media source pinned. Video stream copy is disabled, so fallback requires actual encoding rather than remuxing. Compatible known-size video can still direct-play, and audio does not require video dimensions. Regression cases cover native/360p limits, 1080p/4K, one-pixel boundary violations, portrait video, unknown dimensions, selected versions and resume retention.

## First hardware pass

1. Launch, add your server, use the system keyboard, sign in, close and relaunch.
2. Browse movies and a show's seasons/episodes; search and change favorite/watched state.
3. Play a compatible H.264 MP4, pause, seek forward/backward and stop.
4. Play HEVC or MKV content requiring transcoding; check server playback/transcode logs.
5. Pick an alternate audio track and ASS/PGS subtitles; verify stream changes retain position.
6. Stop after at least 30 seconds; check resume in another Jellyfin/Moonfin client, then resume on Vita.
7. Suspend and wake; stop, return to browsing and repeat playback several times.
8. Test music, live TV and Seerr if enabled on your server.

Report the VPK version, Vita model/firmware, server/version, playback method and the first failing step. Do not post `settings.json`, passwords, access tokens or URLs containing `api_key`.
