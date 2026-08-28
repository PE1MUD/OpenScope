
## Delta 59 Mk4 — workspace-only resize geometry compile fix
- Compile fix: convert Win32 `RECT` width/height (`LONG`) to `int` before `std::max`, avoiding MSVC mixed-type template errors.
- Rebuilt directly from the original Delta58 baseline.
- Keeps the native Windows title bar and existing Source menu unchanged.
- WM_SIZING now measures actual outer-window chrome (native frame/title bar + Source menu) and applies 4:3/16:9 only to ScopeWorkspace.
- Normal aspect switches preserve client chrome and resize only the workspace portion.
- No frameless/titlebar overlay/window-style code added.

# OpenScope 0.8.4 - rolling delta log

## Delta 01 - Performance floaty consistency
- Began centralising waveform phase names and colours.
- Mouseover/detail terminology was aligned with the phase bars.

## Delta 02 - build fix
- Added the missing helper declarations required by delta 01.

## Delta 03/04 - experimental waveform Spout reuse
- Experimented with reusing prepared waveform luma for the Spout renderer.
- Build fix followed.

## Delta 05 - rollback waveform Spout reuse
- Reverted the reuse experiment after it caused a fullscreen timing regression.
- Kept Screen waveform and Spout waveform performance accounting separate.

## Delta 06 - Performance Floaty reporting v2

This delta is a reporting/instrumentation cleanup. It deliberately avoids another
waveform render optimisation until the diagnostic view is trustworthy.

### Architecture is explicit in the row names
- `Waveform worker (Screen + Spout) [timeline]`
  - `Screen waveform [timeline]`
  - `Spout waveform [cost]`
- `Vectorscope worker (Screen + Spout) [timeline]`
  - `Screen vectorscope [cost]`
  - `Spout vectorscope [cost]`
- Screen and Spout therefore no longer look like independent workers when they are
  actually serial consumers on one worker thread.
- The old `PC waveform`, `Waveform video`, `PC vectorscope` and `Vectorscope video`
  wording is removed from the floaty.

### New authoritative worker timelines
- The waveform worker now publishes an exact capture-relative top-level timeline:
  - `S` Screen waveform render
  - `V` Spout waveform render
  - `M` measurement/spectrum preparation
  - `E` Qt image publish/emit
- The vectorscope worker now publishes an exact capture-relative top-level timeline:
  - `S` Screen vectorscope render + emit
  - `V` Spout vectorscope render + emit
- This exposes the actual serial relationship between Screen and Spout on each worker.
- It also explains/removes the old floating post-Spout marker: M/E are no longer drawn
  as if they were Screen-waveform sub-phases.

### Waveform phase labels
- The second old `P` phase is renamed at the renderer source:
  - `P` = ScopePhor feedback/history
  - `Q` = phosphor energy -> output image
- `X` is explicitly shown as a dashed render envelope and documented as NOT extra CPU.
- The Screen waveform child row contains renderer phases only.

### Honest timeline vs cost reporting
- Rows are explicitly suffixed `[timeline]` or `[cost]`.
- Capture-relative timelines use real phase timestamps.
- Where only aggregate timings exist (for example internal vectorscope renderer phases),
  the child bar is explicitly a cost breakdown rather than pretending to be chronology.
- Vectorscope cost markers:
  - `A` analyzer/density
  - `P` persistence/glow
  - `C` compose
  - `O` targets/overlay
  - `?` unclassified remainder
- Spout waveform cost markers:
  - `R` trace
  - `P*` aggregate persistence
  - `C` compose
  - `G` glow
  - `O` overlay
  - `?` unclassified remainder

### Tooltip and click-to-pin
- Mouseover and click detail now call the SAME reporting function.
- Clicking any bar stores a copy of the exact `PerformanceSnapshot`; the lower panel is
  genuinely pinned/frozen instead of continuing to show changing live values under a
  pinned label.
- Legacy detail text such as stale `scalar`, stale zero counters and separate old
  tooltip mappings has been removed from the widget.
- Inactive rows explicitly report no published phases instead of recycling old data.

### Scope
Changed files are limited to:
- `src/widgets/PerformanceWidget.cpp`
- `src/widgets/PerformanceWidget.h`
- `src/util/PerformanceStats.h`
- `src/VideoEngine.cpp`
- `src/rendering/WaveformRenderer.cpp`

## Release-note draft
Performance diagnostics were reworked so worker ownership, Screen-vs-Spout serial
execution, phase labels, tooltips and click-to-pin details all describe the same data.
No render optimisation is attempted in this delta.


## Delta 08 - Worker reporting cleanup + heartbeat diagnostics

### Waveform worker layout
- Removed the separate `Waveform raster assist W0` row.
- Raster-assist chunks are now overlaid directly on the `Waveform worker` timeline.
- The Waveform worker row is explicitly named:
  `Waveform worker (Screen + Spout + raster assist) [timeline]`.
- Clicking/hovering the Waveform worker now reports both:
  - top-level Screen/Spout/measurement/publish phases;
  - raster-assist chunk timings.
- This keeps the floaty aligned with the actual four-worker architecture:
  Display worker 1, Display worker 2, Waveform worker, Vectorscope worker.

### Heartbeat diagnostics
Added a `Timing diagnostics [events]` row for rare scheduler/pipeline events:
- `RA` - presenter re-anchor after a long stall/catch-up.
- `PS` - presenter generation skip.
- `WS` - waveform generation skip.
- `VS` - vectorscope generation skip.

The tooltip/click-to-pin detail shows:
- cumulative event count;
- interval since the previous event;
- event-specific value (late microseconds or skipped frames).

A repeating interval near 1000 ms should make a 1 Hz dependency immediately visible.

### Trace log correlation
The same rare events are also emitted through the existing lock-free TraceLogger using
sentinel item IDs, without changing the logger ABI/file format:
- `A001` = presenter re-anchor
- `A002` = presenter generation skip
- `A003` = waveform generation skip
- `A004` = vectorscope generation skip

No renderer optimisation is included in this delta.

## Delta 09 - Screen frame wallclock semantics

- Replaced misleading `Screen video [cost]` with:
  `Screen Frame Cost [wallclock]`.
- The bar now uses `field2Ready`, i.e. capture/timeline origin -> Field 2 ready.
- Display-worker CPU time is intentionally not summed into this bar because the
  workers execute in parallel.
- Renamed `Display compose [cost]` to:
  `Display compose CPU sum [work]`.
- Its tooltip now states explicitly that it is the sum of compose work over both
  fields and all display workers, not wallclock.
- Hover and click-to-pin use the same corrected definitions.

## Delta 10 - Consistent worker/output semantics

- Worker rows are now the ONLY wallclock/timeline rows for waveform and vectorscope.
- `Screen waveform` changed from `[timeline]` to `[cost]`, matching `Screen vectorscope [cost]`.
- Exact Screen-waveform phases (`F/U/T/R/P/Q/C/O/...`) are now overlaid on the
  `Waveform worker` timeline, together with the top-level Screen/Spout envelope
  and raster-assist chunks.
- Clicking/hovering `Waveform worker` now contains all wallclock/start-time information.
- Clicking/hovering `Screen waveform [cost]` now contains cost breakdown only and
  explicitly says that chronology belongs to the parent worker.
- This yields one consistent model:
  - parent worker row = when work happened;
  - child Screen/Spout row = how much each type of work cost.
- Vectorscope already followed this child-cost model; this delta brings waveform
  into the same model.


## Delta 11 - Conditional 4-worker F + inspectable performance timeline

### Conditional Frequency Compensation
- Frequency Compensation (`F`) is now demand-driven.
- If compensation is enabled but no active downstream consumer needs compensated Y,
  no F task is created.
- Mouse-over / pinned worker detail reports exactly:
  `F: No consumer; not running`.
- If compensation itself is disabled, detail reports:
  `F: Disabled; not running`.

### Four real workers execute F
The capture thread now only orchestrates the F job and waits at its barrier.
F chunks are claimable by the four real execution workers:
1. Display worker 1
2. Display worker 2
3. Waveform worker
4. Vectorscope worker

Display workers retain priority for previous-frame Field 1 / Field 2 work and therefore
do not abandon the sacred display critical path just to take an F chunk.

The Performance floaty publishes F chunks independently on all four worker lanes and
shows `F complete @ xx.xx ms` in worker details.

### Floaty terminology
- Removed `raster assist` from the visible Waveform-worker name and detail wording.
- R/X chunks remain visible where useful, but are presented simply as work on the worker.

### Timeline inspection controls
The Performance floaty is now inspectable instead of being a fixed 0..80 ms picture:
- Added a `Pause` / `Resume` button.
  - Pause freezes the currently displayed PerformanceSnapshot.
  - Incoming 40 ms updates are ignored while paused.
  - Resume continues with the next live snapshot.
- Added timeline zoom from x1 through x16.
  - x1 = 80 ms visible.
  - x16 = 5 ms visible.
  - Zoom is centred around the current view.
- Added a horizontal timeline scrollbar.
  - At zoom > x1 it pans through the 0..80 ms frame timeline.
- Scale labels and deadline guides follow the selected zoom/pan window.
- Off-screen phases are clipped instead of collapsing misleadingly onto an edge.
- Click-to-pin and mouse-over detail continue to operate on the displayed snapshot.

### Scope
This is deliberately still the first scheduler proof-step, not the general task scheduler rewrite.
R/X four-worker scheduling and vectorscope multi-worker chunking remain subsequent steps.


## Delta 12 - build fix for Delta 11
- Restored the local `assistTimelineGeneration` definition used when resetting the
  waveform/display R/X diagnostic timelines.
- Delta 11 removed the old F-seeding block which had also contained this declaration;
  the three remaining reset calls therefore referenced an undefined identifier.
- No runtime/scheduler behaviour change.


## Delta 13 - Windows worker priority policy
- The two Display phase workers now explicitly use `THREAD_PRIORITY_HIGHEST`.
- Waveform and Vectorscope workers explicitly use `THREAD_PRIORITY_NORMAL`.
- The process priority class is intentionally left unchanged.
- `THREAD_PRIORITY_TIME_CRITICAL` is deliberately not used.
- Existing presenter timing/MMCSS behaviour is unchanged.
- Goal: let Windows favour Field 1 / Field 2 / 50 Hz video-output work while
  allowing instrument work to yield under contention.


## Delta 14 - hard video path isolated on HIGH-priority Display workers

This deliberately revises the Delta 11 four-worker F experiment after the worker-priority
policy made the latency consequence visible.

### Critical-path ownership
- Frequency Compensation (`F`) is now executed only by:
  - Display worker 1 (`THREAD_PRIORITY_HIGHEST`)
  - Display worker 2 (`THREAD_PRIORITY_HIGHEST`)
- Waveform and Vectorscope workers (`THREAD_PRIORITY_NORMAL`) no longer wake for,
  claim, or execute F chunks.
- The capture thread remains the F orchestrator/barrier only.

### Reason
A NORMAL-priority worker must never be part of a barrier on which the hard video path
waits.  Four-worker F can improve raw throughput, but it also lets a delayed
Waveform/Vectorscope worker hold up Field 1 / Field 2.  Two HIGH-priority workers make
the critical path more deterministic.

### Video island
The intended hard-video island is now:
`conditional F -> N -> D -> C1/S1 -> C2/S2 -> Field 1/Field 2 + Video Spout`

All of that work stays on the two HIGH-priority Display workers.  Instrument work remains
best-effort and may degrade independently under CPU pressure.

### Floaty
- F chunks are shown only on Display worker 1 and Display worker 2.
- `F complete @ xx.xx ms` remains available in Display-worker detail.
- If compensation is enabled but unused, the detail remains exactly:
  `F: No consumer; not running`.
- Waveform/Vectorscope lanes no longer imply participation in F.

### Threading policy
N and D remain on the existing Display workers.  They are intentionally not spread onto
the NORMAL-priority instrument workers: these phases are already short, and predictable
critical-path latency is more valuable than adding more barriers/wakeups.


## Delta 15 - Waveform waits for F only

### Floaty correctness
- Removed the stale renderer-side `F` block from the Waveform worker lane.
- F remains CPU work only on Display worker 1 and Display worker 2.

### Prerequisite visualization
- The Waveform worker now shows a dashed `wait F` interval from capture origin
  through the F-complete barrier.
- `wait F` is explicitly reporting a dependency/wait, not CPU utilisation.
- Mouse-over / pinned detail says the same thing.

### U
- Waveform waits for **F only**.
- After F, Waveform may read its selected line from the frequency-corrected frame
  and perform its own local selected-line upsample (`U`) on the Waveform worker.
- Therefore the Waveform `U` phase remains on the Waveform worker timeline.
- There is no dependency on a Display-worker U before Waveform may proceed.


## Delta 18 - recovery + early independent instruments
- Repairs Delta 16/17 fallout.
- Restores `VectorscopeRenderer::setColorizeGamutErrors()` and conditional gamut-error drawing.
- WaveformRenderer is untouched; local one-line F uses the existing
  `LumaHighFrequencyCompensator::processRange()` in VideoEngine.
- Single-line Waveform starts immediately from its copied raw selected line, performs local F,
  then its existing U/R/X path.
- All Lines still consumes the post-F full frame.
- Vectorscope starts immediately from an immutable raw frame.
- Vectorscope parent timeline overlays A/P/C/O inside S/V with Screen/Spout-aware details.
- Full-frame F/N/D/C/video stay exclusively on the two HIGH-priority Display workers.


## Delta 19 - no consumer really means no full-frame video work
- Full-frame Frequency Compensation now counts only real full-frame Y consumers.
- Screen/Spout video are full-frame F consumers.
- All Lines Waveform (`selectedLine < 0`) is a full-frame F consumer.
- Single-line Waveform is **not** a full-frame F consumer; it keeps using its own
  local one-line F and can start immediately.
- Vectorscope remains independent of F.
- When both Screen video and Video Spout are disabled, `field1Ready` and
  `field2Ready` are explicitly reset to 0.
- Therefore `Screen Frame Cost [wallclock]` also becomes 0 instead of showing a
  stale value from the previous video generation.


## Delta 20 - inactive video branch + truthful Display-worker task details
- When there is no Screen-video or Video-Spout consumer, the video timing branch is
  visually inactive instead of looking successfully green:
  - Field timing -> grey `inactive / no video consumer`
  - Field 1 ready -> grey `inactive`
  - Field 2 ready -> grey `inactive`
  - Screen Frame Cost -> grey `inactive / 0.00 ms`
- No red/green deadline status glyphs are drawn for an inactive video branch.
- Display-worker mouse-over/pinned details now list **all work visible on that worker**:
  normal Display phases plus opportunistic Waveform R/X chunks.
- A Display worker that is doing R/X work is therefore no longer described as
  `inactive / no published phases`.
- `F: No consumer; not running` remains as a separate statement about F only; it no
  longer reads as if the whole worker has nothing to do.


## Delta 21 - timing trigger + phase colours
- Right-click inside any timeline bar to arm an automatic pause at that
  capture-relative timing value.
- The graph freezes on the first incoming snapshot where any of the four worker
  chronologies extends beyond the threshold.
- The armed value is shown as a dashed vertical line and as
  `AUTO-PAUSE > xx.xx ms` in the toolbar.
- Right-clicking while paused resumes and arms the new trigger.
- Left-click remains snapshot PIN.
- Waveform R and X are coloured again (warm R, lavender X) instead of generic grey.


## Delta 22 - split waveform P/G/Q + High-prio worker naming
- `Display worker 1/2` renamed to `High-prio worker 1/2`.
- Worker detail now describes their role as:
  `video critical path + opportunistic instrument work`.
- Waveform performance timing is split more truthfully:
  - `P` = ScopePhor feedback/history only
  - `G` = image clear + graticule
  - `Q` = phosphor energy -> output image
- `P` no longer includes graticule drawing or Q composition.
- ScopePhor at 0 skips `applyScopephorFeedback()` entirely.
- ScopePhor history is cleared once when the setting transitions to 0, rather than
  performing zero-setting housekeeping every frame.
- Screen waveform child cost row and tooltip now expose P/G/Q separately.
- Beam Glow remains independently conditional inside Q; with Beam Glow = 0 its
  glow pass does not run.


## Delta 23 - dynamic scheduler priority ownership
- `High-prio worker 1/2` renamed to `Video worker 1/2`: priority is now attached
  to work, not permanently to those threads.
- Video workers run at `THREAD_PRIORITY_NORMAL` while idle or doing opportunistic
  waveform R/X assist.
- Actual hard-video phases and full-frame F jobs promote the executing Video worker
  to `THREAD_PRIORITY_HIGHEST`, then immediately return it to NORMAL.
- Waveform and Vectorscope remain NORMAL by default.
- When the hard-video path is idle, exactly one instrument worker may acquire an
  instrument boost token and run at `THREAD_PRIORITY_ABOVE_NORMAL` for that
  generation; the lease automatically returns it to NORMAL afterwards.
- The instrument boost is intentionally ABOVE_NORMAL rather than HIGHEST. This
  guarantees that newly arriving HIGHEST hard-video work wins immediately, even if
  an instrument generation is halfway through a long render.
- Only one instrument boost token exists, so Waveform and Vectorscope cannot both
  become boosted and reproduce the same scheduler contention elsewhere.


## Delta 24 - split waveform base clear from graticule
- The former `G` phase is split again to expose the expensive strands separately:
  - `B` = `image_.fill(Qt::black)` / base image clear only
  - `G` = QPainter graticule draw only
- Waveform worker timeline records B and G independently.
- Screen waveform child-cost row and detail tooltip report B and G independently.
- No rendering algorithm is changed in this delta; this is instrumentation only,
  so the next captured >40 ms frame tells us whether the cost is framebuffer
  bandwidth/clear or actual graticule painting.


## Delta 25 - waveform QImage double buffering
- WaveformRenderer now owns two same-sized QImage transport surfaces:
  - `image_` = worker-owned render target
  - `publishedImage_` = completed frame exposed to Qt
- Both buffers are allocated together on construction and resize.
- A completed waveform frame swaps render/published buffers only after all drawing
  and overlay work has finished.
- `image()` now returns `publishedImage_`; the next render therefore starts in the
  other buffer instead of immediately writing into the QImage just emitted to Qt.
- This targets the captured `B base image clear` spikes where `image_.fill()` could
  trigger Qt implicit-sharing detach/allocation/copy while the GUI still held the
  previously published image.
- Existing B/G/Q instrumentation remains unchanged so the effect is directly visible
  in the performance floaty.


## Delta 26 - vectorscope QImage double buffering
- Applies the same transport-surface fix proven useful on the waveform to
  `VectorscopeRenderer`.
- `image_` is now the worker-owned render target.
- `publishedImage_` is the completed frame exposed through `image()`.
- Both buffers are allocated/resized together and initialized black.
- The QPainter is explicitly ended before the two QImages are swapped.
- The completed vectorscope frame is published only after analyzer composition,
  graticule, gamut overlay and info overlay are finished.
- The next generation therefore renders into the alternate QImage rather than
  immediately modifying a surface still shared with Qt/GUI or Spout.

## Delta 27 - Screen waveform X envelope visibility
- Restored `X` to the pinned waveform Screen sub-phase details.
- Added `X` to `Screen waveform [cost]` as a dashed full-render envelope.
- `X` does not consume cost width: it overlays the cost breakdown because it represents the complete Screen waveform render envelope, not extra CPU work.
- Added a visible `X` marker at the end of that envelope for quick correlation with the worker timeline/details.

## Delta 28 - truthful X reporting + chronological worker details
- `Screen waveform [cost]` now draws the real measured `X` Screen-render envelope as a dashed overlay across its measured duration; it is not appended as extra cost.
- Screen-waveform tooltip/pinned details now explicitly report the measured `X` envelope duration/start and state that it overlaps sub-phases and is not additive.
- Waveform worker detail reporting now merges renderer sub-phases and Video-worker assist chunks, then stable-sorts them by capture-relative start time. `X` is retained in that chronology instead of being filtered out.
- Video worker detail reporting now merges hard-video phases and opportunistic waveform-assist phases and stable-sorts the combined list by start time.
- Generic waveform/vectorscope worker phase detail output is sorted by start time defensively, so textual reporting follows the same left-to-right order as the timeline bars.
- No renderer or scheduler behavior is changed; this delta is performance-floaty reporting only.

### Release-note update
Performance diagnostics now report the measured Screen-waveform `X` envelope consistently in the cost bar and details, while worker detail text is ordered chronologically to match the visual timelines.

## Delta 29 - Build delta in application title
- Added `OPENSCOPE_DELTA` to the generated build version header.
- The main OpenScope window title now includes the running delta number.
- With a valid render size the title is `OpenScope V0.8.4 - <width>x<height> - Delta 29`.
- Without a valid render size it is `OpenScope V0.8.4 - Delta 29`.
- Delta number is set once in `CMakeLists.txt`, so each future delta only needs that value bumped.

### Release-note update
The application title now identifies both the release version and exact delta build, making it easy to verify which patch is actually running.

## Delta 30 - Screen waveform exact chronology / X visibility
- Changed `Screen waveform` from an aggregate `[cost]` row to an exact capture-relative `[timeline]` row.
- The child row now draws the published renderer events at their real `startUs` positions in chronological order.
- `X` is drawn as a normal visible phase on this diagnostic row, instead of only as a dashed envelope that could be visually lost behind other phases.
- Removed the synthetic `?` remainder from the Screen waveform row; the row no longer pretends unknown aggregate cost is chronology.
- Tooltip and pinned details for Screen waveform now list the same events chronologically with start, duration and end time.
- The application build delta is bumped to 30.

### Release-note update
Screen waveform diagnostics now show the actual renderer chronology directly, including visible `X` phases, with no synthetic trailing `?` remainder.

### Delta 31 — strict chronological worker event stream
- Waveform worker details are now one strict capture-relative event stream; no semantic regrouping.
- Every row shows `start-end | worker | phase | duration | description`.
- Main worker phases, Screen waveform phases and waveform-worker assist chunks are merged and stable-sorted by start time.
- `WF` identifies work executed by waveform worker 0; assist chunks in `waveformWorkerAssist` are worker-0 work by construction.
- Removed the accidental duplicate `waveformScreenDetails` function parameter line from Delta 30.
- Delta title support from Delta 29 is included cumulatively; build identifies itself as Delta 31.


## Delta 32 - strict chronology worker identity fix
- Waveform detail stream now merges waveform-worker and both video-worker assist timelines.
- Assist events report the actual executing worker as `WF`, `V1`, or `V2`.
- Removed the top-level Screen `S` envelope from the detailed event stream; it wrapped the same work and produced a misleading duplicate first line.
- Removed renderer-timeline `X` records from the detailed stream and uses the per-worker assist `X` records instead, eliminating duplicate X entries while preserving real worker identity.
- `V`, `M`, and `E` remain real serial waveform-worker events and now display their actual phase letters instead of `?`.
- Delta title bumped to 32.


## Delta 33 - True waveform chunk chronology and Spout timeline

- Waveform assist timeline events now carry the real job/chunk index.
- R (trace raster) and X (resolve/output) diagnostics report the real worker plus chunk number instead of generic envelope text.
- Video worker detail views now include both waveform-assist and frequency-compensation chunks, including their chunk indices.
- Screen waveform no longer draws the aggregate R/X wallclock envelopes as if they were executable phases; it draws the actual WF/V1/V2 R/X chunk events at their capture-relative positions.
- Waveform-worker detail reporting likewise suppresses aggregate R/X envelopes and reports the real chunk jobs in strict chronological order.
- Spout waveform is again an honest capture-relative timeline. Its V phase starts where Spout rendering actually starts instead of presenting an aggregate cost bar from t=0.
- Spout internal cost counters remain available in details, explicitly marked as non-positioned cost counters.
- Application delta identifier advanced to Delta 33.
## Delta 34 - composite parallel envelopes
- `Screen waveform [timeline]` now renders parallel raster work as one `RRR` wallclock envelope instead of individual `R` chunks.
- Parallel resolve/output work is rendered as one `XXX` wallclock envelope instead of individual `X` chunks.
- Envelope bounds are the earliest chunk start through the latest chunk end across WF, V1 and V2.
- Individual R/X chunks, worker ownership and chunk numbers remain available on worker timelines/details.
- This change applies to the composite Screen waveform row only; Spout waveform remains capture-relative.
- Application delta number bumped to 34.


## Delta 35 - waveform gap instrumentation + zoom ruler

- The previously unexplained Screen-waveform wallclock gap between `T` (trace preparation) and the first parallel raster chunk is now explicitly instrumented as `K` = `Packet classify / MUD sieve`.
- `K` is published through the normal capture-relative Screen-waveform phase timeline, so the composite row, tooltip and pinned details account for the same work instead of showing an anonymous blue/empty interval.
- Existing detailed MUD trace-log counters remain intact; this adds the missing wallclock phase to the Performance view.
- The Performance time ruler is now adaptive. At high zoom it provides 1 ms tick marks (and 1 ms labels at the tightest view), with progressively coarser major labels when zooming out.
- Composite parallel rendering remains unchanged: the Screen-waveform child row shows one `RRR` raster envelope and one `XXX` resolve envelope rather than serial-looking individual parallel chunks.


### Delta 36 - Empty means empty
- Waveform worker parent `S` (Screen waveform render) is no longer painted as a filled blue block.
- `S` remains visible only as a dashed outline for wallclock-envelope context.
- Only measured child phases receive filled colour, so uninstrumented time remains black and immediately visible.
- Application delta bumped to 36.

## Delta 37 — Worker priority state overlay

- Added a thin red priority-state underline to the individual physical worker timelines.
- Video worker hard-path intervals are marked while the thread is at `THREAD_PRIORITY_HIGHEST`.
- Video-worker frequency-compensation intervals are marked because those jobs are explicitly executed at `THREAD_PRIORITY_HIGHEST`.
- Waveform and vectorscope workers are marked only when they actually own the temporary instrument priority lease (`THREAD_PRIORITY_ABOVE_NORMAL`).
- The red underline disappears when the boost is absent; it is intentionally not represented as an extra phase box or textual event.
- No scheduling or priority policy was changed in this delta; this is diagnostic instrumentation only.
- Display worker timeline capacity was increased to accommodate the state markers without dropping normal phase events.
- Application title delta: 37.


## Delta 38 — deterministic two-pair priority handoff

- Restored the agreed priority ownership model: exactly one worker pair owns `THREAD_PRIORITY_HIGHEST` at a time.
- New frame arrival is an absolute override: Video worker 1 + Video worker 2 immediately receive HIGH priority before F/N/D/C/S scheduling.
- At the end of the mission-critical video path, HIGH priority moves as a pair to Waveform + Vectorscope when either instrument worker is active.
- The old single-owner `InstrumentPriorityLease` was removed; it could boost at most one instrument worker and only tested eligibility once at the beginning of an iteration.
- Instrument activity is tracked as a two-bit busy mask. The first active instrument can claim the instrument pair after video releases it; HIGH returns to the two Video workers only after both instrument workers are done.
- Early waveform/vectorscope wakeups cannot steal priority during a new-frame critical path: a dedicated critical-ownership flag gates the handoff until video explicitly releases it.
- Per-phase Video priority toggling was removed; priority now belongs to the pair for the complete ownership interval rather than being repeatedly dropped between mission-critical phases.
- Existing red priority underlines remain diagnostic; no rendering algorithm or waveform/vectorscope processing was changed.
- Application title delta: 38.


## Delta 39 - Exclusive priority-pair handoff + truthful red underline

- Priority ownership is now an exclusive two-thread pair state:
  - Video critical path: Video worker 1 + Video worker 2 = `THREAD_PRIORITY_HIGHEST`.
  - After critical-path completion: Waveform + Vectorscope = `THREAD_PRIORITY_HIGHEST`.
  - After both instrument workers are idle: ownership returns to the video pair.
  - New-frame arrival remains the hard override back to the video pair.
- Pair transfer demotes the previous pair before promoting the next pair, preventing the
  four-HIGH overlap seen in Delta 38.
- Thread registration now shares the same priority-state mutex, preventing a newly
  registered worker from observing a half-completed handoff.
- Priority transitions are timestamped. The thin red underline is generated from those
  actual ownership epochs instead of inferring an entire worker iteration from one
  end-of-iteration priority sample.
- Video worker priority underlines are no longer unconditional.
- No rendering algorithm or scheduling cadence change beyond the intended priority-pair
  ownership fix.


## Delta 40 - visible waveform preparation + stale-lane cleanup

- Added explicit waveform preparation phases after MUD classification:
  - `L` = raster load/cost analysis (AA/stitch decisions, active bounds and per-column workload model).
  - `J` = chunk/job partition preparation before the parallel `R` queue starts.
- This makes the formerly large black `K -> RRR` gap accountable without inventing filler for harmless micro-gaps.
- Disabled performance lanes are invalidated instead of freezing their last snapshot. Waveform and vectorscope Screen/Spout metrics are cleared when their consumer is disabled; when both consumers for an instrument are off, its published worker timeline is cleared too.
- Existing Delta 39 exclusive two-pair priority handoff and red priority underlines are unchanged.
- Application title delta: 40.


## Delta 41 - fix Delta 40 waveform phase instrumentation build

- Fixes the Delta 40 compile errors caused by `frameTimer` / `recordPhase` being referenced inside `renderCurrentPhosphorEnergy()`, where those renderSingleLine locals are out of scope.
- Passes the capture-relative raster timeline base explicitly into `renderCurrentPhosphorEnergy()`.
- Uses a local phase clock and local phase recorder for `L` (raster load/cost analysis) and `J` (chunk/job partition).
- Keeps Delta 40 stale-lane cleanup and Delta 39 exclusive priority handoff unchanged.
- Application header reports Delta 41.


### Delta 42 - pre-raster gap instrumentation
- Instruments the formerly black interval between `K` and the first raster work instead of painting a synthetic block over it.
- `E` = full target energy-buffer allocation/zero-fill.
- `H` = glow subpixel-kernel preparation.
- `A` = analytic-AA / RED-WHITE stitch setup.
- Existing `L` and `J` phases now have explicit PerformanceWidget descriptions: raster load/cost analysis and chunk partition/job dispatch.
- Raises waveform phase-event capacity from 12 to 20 so the extra diagnostics cannot evict later real phases.
- Application delta marker bumped to 42.


### Delta 43 - persistent waveform energy target
- Makes the full-resolution waveform beam-energy target persistent for an unchanged target width/height.
- Recreates the target only when the render resolution changes; steady-state frames reuse the existing allocation.
- Splits diagnostics: `E` = resolution-triggered allocation/resize, `e` = steady-state clear/reset.
- Adds the missing PerformanceWidget glyph/color mappings for `E/e/H/A/L/J`, so instrumented phases no longer appear as `?`.
- Avoids a redundant clear immediately after a fresh zero-initialized allocation.
- Application delta marker bumped to 43.

## Delta 44 - Four-worker waveform assist + priority-slot decharge
- Keeps the hard rule of **exactly two OpenScope workers at HIGH priority**.
- New-frame override remains absolute: Video worker 1 + Video worker 2 own HIGH immediately.
- Video workers now retain HIGH until the completed video frame is actually committed to the presenter slot and the presenter is notified; priority is no longer handed off merely because the last conversion phase ended.
- After video commit, HIGH goes to Waveform + Vectorscope while their own instrument tasks are active.
- When an instrument worker finishes its own task, it immediately decharges its HIGH slot back to a video worker but remains available at NORMAL priority to help waveform work.
- The Vectorscope worker can now consume the same Screen-waveform R/X chunk queue as WF/V1/V2 while it has no newer vectorscope frame to process.
- Screen-waveform R/X composite envelopes count the workers that actually participated, so the label can become RRRR/XXXX when all four workers contribute.
- Vectorscope worker timeline/details now include any R/X chunks it assisted with.
- CatWuzle per-worker accounting expanded from three to four workers.
- Delta 43 persistent energy-target optimisation and its E/e instrumentation are retained.


### Delta 45 — Qt `slots` build fix
- Renamed local `slots` counter in `postVideoPriorityMask()` to `highSlots`; Qt expands `slots` as a macro, which caused the C2059/C2143 cascade in `VideoEngine.cpp`.
- No scheduling-policy change versus Delta 44.

## Delta 46 - VS assist accounting cleanup
- Reset the vectorscope worker's waveform-assist timeline at the same capture/frame boundary as WF/V1/V2.
- Prevent stale VS R/X chunks from accumulating across waveform frames.
- Added generation-coherence guards in the composite Screen waveform lane and worker detail views, so a stale assist snapshot can never be mixed into a newer frame.
- Fixes the long duplicated VS R/X bookkeeping/tooltip and the stale blocks that remained visible far to the right of the active timeline.


## Delta 47 - Live priority ownership + F11 aspect refresh
- Priority underlines are now derived at snapshot time from the central priority-mask transition history instead of being baked into worker snapshots when those workers finish.
- This makes late priority decharge/return to V1/V2 visible and keeps the red ownership line authoritative even after the video workers have already committed their frame.
- F11 fullscreen aspect-ratio changes no longer run the normal top-level resize path. The saved restore geometry is updated to the new aspect and fullscreen/layout geometry is reasserted on the next event turn so the fullscreen viewport is recalculated immediately.


## Delta 48 - Priority underline generation alignment
- Fixed the Delta 47 priority underline regression.
- Each worker's red HIGH-priority underline is now reconstructed against that worker row's own capture generation and diagnostic origin.
- Video worker 1, video worker 2, waveform worker and vectorscope worker no longer share a waveform-derived timing origin.
- Late VS/WF decharge transitions remain visible because ownership still comes from the central priority-mask transition log.
- Scheduling behaviour itself is unchanged; this delta fixes performance-view accounting only.


## Delta 49 - Frame-local priority underlines
- Clips every red HIGH-priority ownership line at the next capture-generation boundary instead of extending the reconstruction interval to `now`.
- Prevents a later frame's priority handoff from appearing as a second red WF/V1/V2 segment on an older worker row.
- Applies the same frame-local clipping to both video workers, waveform worker and vectorscope worker.
- Scheduling behaviour is unchanged; this is performance-view accounting only.
- A vectorscope row can legitimately have no red segment when its own vectorscope task finishes before video commit; in that case it never actually receives a HIGH slot for that frame and remains a NORMAL-priority waveform helper.

## Delta 50 - Critical ownership + frame-local priority truth + lower legality fringe
- Display workers may no longer claim waveform R/X while the new-frame critical ownership flag is set, closing the capture-to-display-start race where R/X could begin before N/D/S/video commit.
- Priority underline reconstruction now resolves both capture-counter and captureTickNs timeline identities to the same capture slot.
- Frame clipping is based on the next diagnostic time origin, not incomparable generation-number encodings; late handoffs stay in the correct frame.
- V1/V2 can therefore show a returned HIGH slot later in the same frame, while WF/VS cannot inherit red segments from the following frame.
- Lower illegal-luma presentation keeps the intended 0.298 V signal threshold but compensates the finite AA/glow beam footprint so a legal 0.300 V black trace no longer grows an immediate red lower fringe.

## Delta 51 - Persistent QImage transport pool / Base-clear COW removal
- Removes the expensive implicit-sharing penalty hidden inside waveform `B` (Base image clear).
- The completed waveform QImage is handed to the UI/Spout path by implicit sharing; clearing that same shared image on the next frame forced Qt to detach and deep-copy the entire previous framebuffer before writing black.
- Adds two same-resolution spare QImage transport surfaces per WaveformRenderer. Steady-state rendering swaps onto an already allocated detached surface before `B`, so no old-frame copy is required.
- The transport pool is recreated only when target resolution changes, matching the persistent-buffer policy already used for the phosphor-energy target.
- If all three surfaces are still held by consumers, a rare back-pressure fallback allocates a fresh target rather than copying the old image.
- Applies the same acquire-before-clear path to All Lines rendering.
- Delta marker bumped to 51.


## Delta 52 - Spout waveform V internal chronology

- Instrumented the complete Spout-waveform `V` envelope using the renderer's own exact phase events.
- Added a capture-relative `waveformVideoPhases` diagnostic timeline, published atomically with the existing waveform diagnostics.
- The Spout waveform child row now shows real `U/e/H/A/L/J/R/X/B/G/C/Q/O/...` work instead of one opaque `V` block.
- Pinned/hover details show start/end, phase, duration and description for every published Spout renderer phase.
- No optimization or worker-count change yet: this delta is deliberately measurement-first, so the next change is driven by evidence.


## Delta 53 - Spout V worker-row truth

- The Waveform worker row now expands the Spout `V` section into the same exact renderer phases already shown by the Spout child timeline/details.
- `V` remains visible only as an unfilled dashed parent envelope; it no longer hides the real `U/e/H/A/L/J/R/X/B/G/C/Q/O/...` work.
- This is display/accounting only. No Spout rendering, worker scheduling or parallelism has changed yet.
- Establishes a clean visual baseline before the next delta parallelises the expensive Spout `R` trace raster.

## Delta 54 - Herbie Spout R visibility fix

- Fixes Delta 53 worker-row accounting: the Spout renderer's real `R` Trace raster phase was accidentally filtered together with the Screen renderer's synthetic/assist `R` handling.
- The Waveform worker row now draws Spout `R` inside the dashed `V` parent envelope, followed by the real `B/G/C/Q/O/...` tail phases.
- The Spout child timeline/detail output is unchanged and remains the reference chronology.
- Display/accounting only; no renderer scheduling or parallelisation yet. The planned multi-worker Spout `R` change moves to the next delta.


## Delta 55 - Herbie report chronology truth

- Fixes the pinned Waveform-worker report so it matches the worker-row timeline.
- Suppresses outer `V` as a serial event in the authoritative chronology; `V` remains a visual envelope.
- Adds the real Spout waveform renderer sub-phases (`U/T/K/e/A/L/R/B/G/C/Q/O/...`) at their capture-relative positions, tagged `WV`.
- No Spout raster parallelisation yet; this remains the clean pre-optimisation measurement baseline.


## Delta 56 - Scrollable diagnostics + shared selected-line reconstruction

- Adds a vertical scrollbar to the lower pinned/details panel so long authoritative chronology reports remain inspectable without growing the performance window.
- Reuses the Screen waveform renderer's already reconstructed 4x selected-line luma for the immediately following Spout waveform render.
- The Spout renderer skips its duplicate `U` FIR upsample only when Screen rendered the same single selected line in the same worker iteration; All Lines and Spout-only operation retain the normal local reconstruction path.
- The reused luma is copied into the Spout renderer's own small working vector, keeping renderer ownership independent.
- No Spout `R` parallelisation yet.

## Delta 57 - Diagnostics scrollbar viewport fix

- Fixed the pinned diagnostics text viewport: scrolling now moves a full-height content rectangle behind the clipped viewport instead of translating a viewport-sized draw rectangle.
- This prevents the report from going blank after the first visible page while keeping the existing vertical scrollbar behavior.
- No scheduling, rendering, or shared-luma behavior changed from Delta 56.


## Delta 58 — Cerium: parallel Spout waveform raster

- PAL/Spout waveform `R` trace raster now uses the existing shared WF/V1/V2/VS assist queue.
- The hard scheduling rule remains unchanged: at most two workers are HIGH priority; the other helpers remain NORMAL priority.
- Spout assist jobs use internal `r`/`x` tags so Screen and Spout accounting cannot be mixed; the UI renders them compatibly as `R`/`X`.
- Spout child timeline now shows a parallel `RR..`/`XX..` wallclock envelope while worker rows retain the real per-worker chunks.
- Pinned Spout/worker reports show the actual worker ownership for parallel Spout chunks.
- CatWuzle worker accounting expanded to four worker IDs so VS assist is no longer folded into V2.
- No change to waveform appearance or priority arbitration.

### Delta 59 Mk5 — cleanup repair
- Repair package now explicitly restores `src/MainWindow.h` from the clean Delta58-derived tree.
- Removes stale `moveEvent`, `resizeEvent`, `showEvent`, and `hideEvent` declarations left behind when a previous titlebar delta had been applied to the working tree.
- Keeps the Delta59 Mk4 workspace-only aspect/resize geometry unchanged.
- Delta package intentionally includes `MainWindow.h`, `MainWindow.cpp`, `CMakeLists.txt`, and this README so it can safely repair a tree contaminated by Delta63/64 titlebar experiments.


## Delta 60 — display timeline publish-order fix

- Fixes intermittent `? Unknown display phase` entries in the pinned Video worker details.
- `DisplayPhaseTimelineStats::append()` now writes phase/start/duration first and release-publishes `count` only after the slot is complete.
- Removes the race where the Performance UI could snapshot a newly counted but not-yet-written event, producing stale/zero phase data and misleading durations.
- Diagnostic/accounting change only; no video processing, waveform rendering, worker scheduling, priority, or Spout behavior changed.

## Delta 62 - Beam-glow gap instrumentation

- Adds waveform phase `g` = `Beam glow stamp / apply`.
- The existing local Beam Glow stamping pass is now published on the waveform phase timeline with capture-relative start and duration.
- This specifically exposes the previously blank interval after the final trace-raster `R` chunk and before resolve/output `X` when Beam Glow is greater than zero.
- No render, scheduler, worker, Spout, or glow algorithm behavior is changed; this delta is diagnostics-only apart from the version marker.


## Delta 63 - Parallel Beam Glow stamp/apply

- Splits waveform `g` (Beam glow stamp/apply) over the existing shared WF/V1/V2/VS assist queue.
- Glow jobs own disjoint output-row bands, so they write the shared phosphor-energy target without atomics or per-worker full-frame buffers.
- The shared assist executor remains the synchronization barrier before resolve/output `X`.
- Parallel `g` chunks now expose real worker ownership in Performance diagnostics.
- No AVX2 changes yet; this delta isolates the wallclock gain from worker spreading alone.


## Delta 66 - Glow label only

- Built directly from the known-good Delta63 presentation.
- Leaves all physical WF/V1/V2/VS worker lanes and all X/R rendering unchanged.
- Keeps the existing correctly positioned aggregate Screen-waveform `g` span and changes only its text to repeat `g` once per worker that actually executed a parallel glow chunk (for example `gggg`).
- Removes the Delta64/65 extra glow-envelope approach entirely, so glow is not duplicated or shifted after `X`.
- No renderer, scheduler, worker, Spout, or timing behavior changed.

## Delta 67 - Beam Glow real workload partition

- Fixes the Delta63 Beam Glow parallelisation so assist workers no longer rescan and recompute the complete waveform polyline independently.
- Glow sample positions/subpixel phases are prepared once, then bucketed only to the disjoint output-row jobs whose 5x5 kernel can touch those rows.
- Each WF/V1/V2/VS glow job therefore processes only its relevant samples while retaining race-free row ownership and the existing assist barrier before resolve/output X.
- Preserves sample accumulation order within each destination row/pixel; no intended visual Beam Glow change.
- No AVX2 changes yet; this delta isolates the gain from removing duplicated parallel work.

## Delta 68 - Parallel Beam Glow sample preparation

- Removes the new serial gap introduced by Delta67 before the parallel `g` jobs.
- Splits the expensive polyline-to-glow-sample preparation over the existing WF/V1/V2/VS assist queue as phase `h` (`Glow sample preparation`).
- Each `h` job owns a contiguous segment range and writes only private row-band buckets; no locks or shared vector writes are required.
- Glow row workers consume those private buckets in segment order, preserving the original sample accumulation order per output band.
- Maps each sample directly to the one or two row-band jobs its 5x5 kernel can touch instead of testing every band.
- No AVX2 changes yet; this isolates the wallclock gain from parallelising the previously serial preparation pass.


## Delta 69 - Close the Beam Glow accounting gaps

- Diagnostics-only follow-up on Delta68: every wallclock interval between the end of trace raster `R` and the first resolve/output `X` now gets an explicit waveform phase.
- Adds `s` = Glow setup / bucket allocation (bounds scan, row-band setup, private bucket construction).
- Adds an aggregate `h` = Glow sample preparation wallclock marker around the existing parallel prep dispatcher; per-worker `h` assist chunks remain available independently.
- Adds `y` = Resolve setup / scratch allocation between the end of Beam Glow and the first `X` resolve job.
- No glow math, worker scheduling, resolve math, Spout behavior, or output pixels are changed. This delta is measurement-only apart from the version marker.


## Delta 70 - Parallel B and Q waveform phases

- Spreads waveform `B` (Base image clear) over the existing WF/V1/V2/VS assist queue using disjoint scanline bands.
- Keeps `prepareImageForRender()` serial so QImage allocation/detach completes before workers touch scanlines.
- Spreads waveform `Q` (Phosphor energy -> output image) over the same assist queue using disjoint phosphor/output row bands.
- `B` and `Q` assist chunks retain their real worker ownership in the worker timelines; aggregate B/Q wallclock phases remain in the waveform chronology.
- No new threads, no AVX2 changes, no pixel-format or phosphor math changes.

## Delta 71 - Authoritative WF worker timeline

- `Waveform worker (Screen + Spout) [timeline]` now draws directly from the same authoritative event sources as the pinned/hover underwater chronology, filtered to events whose real worker is `WF`.
- Removed the mixed wrapper/aggregate presentation from that lane (`S`/`V` envelopes and duplicate aggregate `R`/`X` paths).
- Screen phases and WF assist chunks now appear on the upper WF lane if and only if the corresponding `| WF |` event exists in the detailed chronology.
- Processing, scheduling, renderer output and worker distribution are unchanged.


### Delta 72 — source-independent Y frequency compensation / no-BM source state
- Y HF / frequency-response compensation is now OpenScope processing, not Blackmagic-only processing.
- Compensation remains active for Philips Pattern ROM sources when enabled.
- The Calibration checkbox is renamed to `Enable Y frequency response correction`.
- Blackmagic composite Y/UV gain controls remain hardware-specific and stay disabled for Philips/no-device operation.
- When probing returns no DeckLink device/driver, the Blackmagic Source action is disabled while OpenScope remains usable with non-DeckLink sources.


### Delta 73 — correct lower illegal-luma threshold
- Corrects the lower illegal-luma signal threshold from `0.298 V` to `0.280 V`.
- Keeps the existing upper threshold at `1.020 V`.
- The finite beam-footprint presentation guard remains unchanged; only the intended signal threshold is corrected.
- No other waveform, worker, source, or calibration behavior is changed.


### Delta 74 - Deferred DeckLink startup probe
- OpenScope now enters the Qt event loop and paints the main window before DeckLink/Desktop Video probing starts.
- Startup begins in a safe no-DeckLink state: Blackmagic source selection and hardware-only Y/UV gain controls remain disabled until a device probe succeeds.
- The DeckLink probe is queued shortly after startup; missing/failed Desktop Video probing can no longer prevent the OpenScope UI from appearing before the probe is attempted.
- Philips Pattern ROM operation remains available without Blackmagic hardware.


### Delta 79 — remove temporary startup diagnostics
- Removes the temporary startup checkpoint logging added in Deltas 75–78 (`OpenScope-startup.txt`).
- Restores `main.cpp`, `MainWindow`, `ScopeWorkspace`, and `ControlWidget` to the clean Delta74 functional startup path.
- Retains all functional changes through Delta74, including generic Y frequency-response correction, the 0.280 V lower luma threshold, no-DeckLink source disabling, and deferred DeckLink probing.
- No processing, rendering, worker scheduling, capture, or UI behavior is otherwise changed.

### Delta 80 — Config (F2) for single-screen fullscreen use

- Added `Config (F2)` directly beside `Source` in the main menu bar.
- F2 opens/hides the Settings tool window while a scope viewport is maximized; F2 remains available in F11 windowless mode.
- Maximizing Video/Waveform/Vectorscope no longer automatically floats Settings over the instrument.
- An already-open floating Settings window is left visible when switching/maximizing scope viewports.
- Returning to Matrix view docks Settings back into its normal fourth quadrant.
- No renderer, worker, capture, Spout, or processing changes.

## Delta 81 - F11 fit, View FPS, power option and luma-limit symmetry

- Keeps Matrix/quad layout behavior unchanged. F11 remains windowless fullscreen and refreshes the video output size after fullscreen geometry settles; VideoWidget continues to aspect-fit 4:3/16:9 and paints all unused monitor area black.
- Adds a `View FPS` Config tab with three columns: `View`, `OpenScope FPS`, and `Spout FPS`, updated once per second from lightweight presentation counters rather than the performance profiler.
- Changes the displayed PAL deinterlaced mode label from `PAL 625i` to `625/50d`.
- Adds `Prevent display sleep / screensaver` to Misc. It defaults to OFF, is persisted, and only when enabled requests Windows display/system execution state.
- Verifies the lower illegal-luma threshold at `0.280 V` and removes the obsolete lower-only AA/glow footprint guard that belonged to the old `0.298 V` limit. The visible legality mapping is now directly linear at `0.280 V` / `1.020 V`, giving symmetric 20 mV margins around 0.300 V black and 1.000 V white.

## Delta 82 - Live wappers and stable View FPS columns

- Waveform diagnostics now publish after a completed worker iteration when either the PC waveform view or waveform Spout renderer is active; Spout-only operation no longer leaves the waveform wappers frozen on an old screen snapshot.
- Clearing waveform diagnostics now also clears the published WF worker-phase snapshot, so switching both waveform Screen and Spout off immediately removes stale worker bars.
- Vectorscope keeps its existing explicit empty-publication path when both Screen and Spout are disabled; no vectorscope processing or scheduling is changed.
- `View FPS` numeric column headers are right-aligned like their values and both FPS columns get equal stable minimum widths; resizing the Config window no longer makes headers and FPS numbers drift apart.
- No capture, rendering, worker scheduling, Spout transport, or signal-processing behavior is changed.

