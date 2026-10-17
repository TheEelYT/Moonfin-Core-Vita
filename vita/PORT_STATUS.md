# Vita port status

Version: **0.1.1 native preview**. This is an initial native implementation, not a declaration of full Moonfin parity. No physical Vita or personal Jellyfin/Emby server is attached to the development environment.

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

Validation completed in the development environment: VitaSDK cross-build and VPK identity/assets checks passed; 56 protocol and local HTTP checks passed. The user confirmed that the corrected VPK installs, launches and signs in to their server. Playback validation is pending.

The first hardware installation attempt reported VitaShell error `0x8010113D`. The original package used truecolor LiveArea PNGs, including an RGBA icon. The corrected package uses opaque 8-bit indexed PNGs for the icon, background and startup image. Package validation now checks their encoding and dimensions, with regression tests rejecting truecolor, alpha and incorrect sizes. The user confirmed that the corrected package installs and launches. Startup then stopped at `ytdl: option not found`: the pinned Vita mpv build disables Lua and therefore omits that option. The unnecessary `ytdl=no` setting has been removed; the user subsequently confirmed successful startup and sign-in.

The portable tests exercise the code used by the native client: URL escaping/base paths, login, profile limits, direct/transcoded resolution, track indices, resume tick conversion, session cleanup, settings persistence, queue ordering, HTTP request transport, redirect refusal and response-size bounds. They use scripted responses and a local HTTP test server. They do **not** establish that a video has played on a real Vita.

## 0.1.1 main-menu crash correction

The uploaded core dump from build `6f1b79f` stops at `0x810b2320` in `brls::Application::handleAction`. Its current-focus pointer references freed login UI memory, already reused for JSON data. The pinned Borealis implementation ignored `giveFocus(nullptr)`; replacing the login screen left that dangling pointer active.

The Vita build now compiles a generated Borealis override that releases null focus and clears focus before activity teardown. The submodule remains unchanged. Home/server screens explicitly focus their new buttons, and root-screen replacement is deferred until action dispatch has finished. Host regression checks execute the actual dependency methods against lightweight UI stubs: the original fails, and the patched methods pass. These checks do not emulate drawing or establish device stability.

Build artifacts now include source-line information and the original ELF for future dump analysis. After installing 0.1.1, repeat sign-in, leave Home idle, navigate its buttons, refresh Home and switch servers. On-device confirmation of this correction is pending.

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
