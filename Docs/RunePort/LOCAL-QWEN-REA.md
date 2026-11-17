# Local Qwen investigations

The Windows-local Qwen worker can collect small, read-only original-engine contracts. Codex selects the question, reviews raw REA evidence, implements changes and runs tests. The worker cannot execute the game, run tests, edit source, inspect screenshots or measure FPS. Do not treat its reports as validated findings.

## Setup and safe operation


`rune-classic-engine` is a read-only snapshot of the authorized original `C:\OpenRune\work\rune-classic\System\Engine.dll`, SHA256 `41458bac7e717a57effbe45d9351664ab79d72d7b821c3103018d057b7c585d0`. It is separate from `vulkan-layer`, which belongs to another project and is not Rune. Configuration was backed up before registration; original binaries and live analysis databases are not modified.

Use the existing dispatcher to check status, start inference, submit a narrow job with `--target rune-classic-engine --max-tools 2`, run the next job, fetch evidence and stop inference. Check for older unstarted jobs first: `run-next` processes the oldest job, not necessarily the one just submitted. Retain its command session until completion and verify the returned task ID. Do not change worker tools, context limits, authentication or runtime configuration to broaden a task.

Good questions identify one native function and request exact predicates, assignments, offsets and uncertainties. Supply a verified address when available rather than asking the model to sift a large export inventory. Split broad questions. Review `status.json`, `report.md` and every cited `E###.json`; check the full evidence if the model received a truncated preview. Keep indirect targets unresolved unless independently identified. Save review decisions separately from the worker's original pending-review status.

## November 2 pilot

Task `20261102-192909-cbd391` investigated `AActor::physMovingBrush(float)` at `0x103d11d0` through its verified exported thunk at `0x103028a1`. It used one pseudocode call and completed in 87.91 seconds, including private Ghidra analysis and report generation. REA closed its private session; inference was stopped afterward. All raw evidence and the Codex review remain outside Git under `outputs/qwen-rune-20261102/` above this checkout, with the registration script, original hashes, disassembly, build and test logs.

The progress/overshoot conditions and clear-before-completion-event order were confirmed against full pseudocode and independent disassembly. Two claims were excluded: an invented “K2_Node” interface, and unsupported position updates on a failed movement call. Its pointer labels also needed correction. The model received a truncated preview even though the retained full pseudocode was complete. The result is **selected verified facts with corrections**, not approval of the entire report.

This pilot consumed approximately 12.3 GiB of whole-device VRAM, with minimum available physical RAM about 4.0 GiB. Those readings include other Windows processes. Do not assume earlier setup headroom is still available or run multiple investigations concurrently.

## Regression coverage and limits

`RunePolyobjProbe.h` now invokes the actual recovered `InterpolateTo`/`InterpolateEnd` scripts on transient Mover and Polyobj fixtures. It checks zero elapsed time, completion at exactly the endpoint, no duplicate terminal completion, a callback starting another leg, and leftover time using the next leg's different rate. The tests pass headlessly on the Mac using LostCrypt and HelLift geometry with isolated save directories. Current production movement already passes: this adds coverage, not a gameplay fix or performance improvement.

The original binary caches a destination index before its inner loop; the port refreshes it per leg. These checks do not claim identical original intermediate positions after chaining within one tick, bit-identical floating-point arithmetic, blocked-movement behavior, rider stability, Android runtime coverage or a full campaign playthrough. No tablet installation or Windows game update was part of this pilot.

## Completed map and movement batches

The subsequent map-data batch reviewed all 94 installed maps. The independent collector's 564,000 fixed-world visibility comparisons match, with no detected invalid lightmap references, non-finite world points or invalid/zero surface normals. This is sampled static correctness coverage, not rendered FPS or visual approval. Qwen's generic ambient warnings and inaccurate dynamic-mover totals were excluded. Hel1b, SinkingShip2 and DM-Havlamahl are candidates for future live profiling based on authored geometry/light-range workload proxies, not confirmed slow maps.

Five original-engine movement jobs also completed. Review accepted IsBasedOn's core ancestry contract, partially accepted SetBase/FarMoveActor facts, and rejected the CheckEncroachment/MoveActor summaries for reversed conditions. Full raw evidence and independent disassembly establish two implementation gaps to test: the port lacks the original push/recheck/reverse-push sequence for blocking non-brush actors, and its SetLocation relocation does not explicitly detach direct riders as the original normal path does. No gameplay fix or runtime reproduction was part of that review. The port's internal base-helper argument convention is reversed but compensated by reviewed callers; do not globally flip it.

Both supervisors completed and verified inference shutdown. Reports are no longer held from the user: their findings were reviewed in the main chat. Raw artifacts/review decisions remain outside Git at `outputs/qwen-map-lighting-20261102/` and `outputs/qwen-movement-contracts-20261102/`, above this checkout, with copies in Windows worker results. Authored orchestration/collector reference source is preserved in [Tools/RuneWorker](../../Tools/RuneWorker/README.md), excluding binaries, decompiler output and generated data.
