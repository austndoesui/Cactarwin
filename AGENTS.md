# Working on ios-cactus (Redmi 6A bring-up)

- Read `../issue.txt` and the CACTUS section at the top of `NOTES.md` first.
- This is an MTK Redmi 6A / cactus experiment; historical SWEET/Qualcomm/UFS
  notes and permissions below apply only to SWEET, not this phone.
- On cactus, `fastboot boot IMAGE` has been observed to execute the *installed*
  boot partition instead of IMAGE. Do not use it as a temporary test or claim
  that it proves an image works. Testing requires an explicitly authorized
  `fastboot flash boot IMAGE` and a verified ALPS boot recovery path.
- Do not flash boot, recovery, userdata, or firmware during source review.
  Preserve all existing uncommitted work. Do not assume a black framebuffer
  proves shim entry; LK may have left the panel disabled.
- Verify CPU topology and MPIDR assignments from cactus hardware DTB before
  enabling SMP; SWEET's eight-core topology is not reliable on Redmi 6A.

- Read `NOTES.md` before starting; it contains the project goal, hardware facts,
  current blocker, local artifact locations and the next concrete action.
- The user switches agents frequently. Update the active handoff in `NOTES.md`
  after each milestone and before stopping. Record commits, CI run IDs, exact
  build/boot results and unfinished work. Distinguish observations from guesses.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve the Android
  installation and partition backups. The user approved (2026-10-15) using
  internal UFS for iOS and said Android may be deleted: the iOS root goes
  into the existing `userdata` partition (no GPT changes). Keep bootloaders,
  recovery, modem, persist and the backups intact, and confirm each
  destructive step with the user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
