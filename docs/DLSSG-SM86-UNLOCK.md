# DLSS Frame Generation on RTX 20/30 (dlssg_sm86 unlock)

This branch lets the experimental `DLSSFG\DLSS_FrameGeneration` effect run on Turing (RTX 20)
and Ampere (RTX 30) GPUs. NVIDIA's runtime refuses DLSS Frame Generation below Ada and returns
`0xbad0000b` from the capability query; this branch loads sdli1995's
[dlssg_sm86](https://github.com/sdli1995/dlssg_for_sm86) unlock into Magpie's own process
before NGX starts, the same files [DLSS-Unlocked](https://github.com/ShyVortex/dlss-unlocked)
ships under `OptiScaler/dlssg_sm86/`.

The unlock lives only in Magpie. Nothing is injected into the captured game: Magpie still
captures the window from outside and draws the result in its own window.

## Build

1. Set `EnableDLSSFrameGeneration=true` and `DLSSSdkDir` (NVIDIA DLSS SDK 310.x) in
   `src/BuildOptions.props.user`. The build copies `nvngx_dlssg.dll` next to `Magpie.exe`.
2. Stage the unlock next to the executable:

   ```
   python scripts/fetch_dlssg_sm86.py --dest bin/x64/Release
   ```

   The script downloads the latest DLSS-Unlocked standalone release, checks the SHA-256 digest
   GitHub publishes for it, and copies `OptiScaler/dlssg_sm86/` to `bin/x64/Release/dlssg_sm86/`.
   Pass `--zip <file>` to use a zip you already downloaded. An edited `dlssg_sm86.ini` is kept.

None of these binaries are committed to the repository.

## Runtime switch

`MAGPIE_DLSSG_SM86` (environment variable):

| Value | Behaviour |
|---|---|
| unset / `auto` | Load when the scaling adapter's CUDA compute capability is 7.5–8.7 |
| `on` | Load on any NVIDIA adapter |
| `off` | Never load (stock behaviour) |

The decision is logged once per process in `logs/magpie.log`
(`dlssg_sm86 unlock loaded …` / `… not loaded: <reason>`). The unlock's own log is
`dlssg_sm86/dlssg_sm86/logs/dlssg_sm86.log`; its settings are in `dlssg_sm86/dlssg_sm86.ini`.

## Status: experiment

Works, but is not a drop-in replacement for in-game frame generation. Measured on an
RTX 3090 Ti, 4K 144 Hz HDR display, Windows 11.

### Image quality (repeatable video test)

A 60 fps clip (Big Buck Bunny, grass scene) played at 30/20/15 fps in a window; the frames the
player skipped are ground truth for the generated ones. SSIM at 640x360, mean / worst 10%:

| Mode | Base fps | SSIM mean | SSIM p10 |
|---|---|---|---|
| DLSS FG 2x + unlock | 30 | **0.978** | 0.971 |
| XeSS FG 2x | 30 | 0.965 | 0.954 |
| Lossless Scaling 2x | 30 | 0.955 | 0.939 |
| Lossless Scaling 3x | 20 | 0.948 | 0.932 |
| DLSS FG 4x + unlock | 15 | **0.952** | 0.937 |
| Lossless Scaling 4x | 15 | 0.939 | 0.911 |

Repeating the previous frame scores ~0.56–0.68. DLSS mostly softens fine detail; Lossless
Scaling doubles it (ghost copies on grass and edges). XeSS FG is 2x-only on non-Arc GPUs.

### Real game (Crimson Desert, 4K borderless)

- The game alone uses ~95% of the GPU at 4K/60. Magpie's 4K frame generation then competes with
  it, and **Windows gives the focused window GPU priority**: with the game focused, Magpie's
  input rate collapsed to ~4 fps (output ~8). Capping the game at 40 fps fixed it
  (steady ~40 in / ~80 out, occasional dips to ~20–28 in).
- Even working, it felt worse than native 60: base 40 instead of 60 plus one held-back frame
  adds latency, and the out-of-process path has no game motion vectors.
- Worth it only when the game holds its base frame rate with GPU headroom to spare.

### Known issues

- **Focused-game GPU starvation** (above). Magpie does not raise its GPU priority.
- **No HDR.** Capture and output are SDR, so on an HDR desktop the image looks brighter and
  washed out. Turn HDR off in the game or in Windows for a like-for-like look.
- **Start-up freeze.** Intermittently the output freezes on the first frame (seen with DLSS and,
  once, XeSS); toggling scaling off and on recovers. Cause not found; suspected in the
  synchronous presentation path.
- **Output is paced by DWM composition**: each generated frame waits for a refresh, so a 60 Hz
  desktop caps output at 60. Run the display at its full refresh rate.

### Setup that mattered

- Display at full refresh rate (144 Hz), not 60.
- A global driver frame cap also caps Magpie's output. Exempt `Magpie.exe` with a per-program
  Max Frame Rate = Off override, and cap the game itself below what it can sustain.
- Borderless full-screen games need fullscreen-mode scaling with "allow scaling maximized" on;
  windowed-mode scaling refuses windows the size of the screen.

## Anti-cheat

The captured game's process is never touched, so this behaves like Lossless Scaling or OBS as
far as the game's anti-cheat is concerned. Games that block screen capture or overlays still
do; this branch does not try to get around that.
