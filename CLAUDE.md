# Rules for AI coding tools in this repo

The Black & White VR Mod puts the original Black & White (2001) in VR. It is
a `ddraw.dll` proxy with MinHook hooks in `runblack.exe` (unofficial patch
v1.42 only), dxwrapper (DirectX 7 to Direct3D 9), a bridge to Direct3D 11,
and OpenXR. The main mode is the God view: the land at the height of the
waist, and the player's hands are the Hand, 1:1. The owner is Buck. He tests
each build in a Quest 3 through Virtual Desktop.

Read `README.md` and `CONTRIBUTING.md` first. The work list is in
`bwvr/docs/roadmap.md`. Each area has one doc in `bwvr/docs/`. The history
is in git, not in the docs.

## Never

- Never add a file of the game to the repo, and never ship one. No art, no
  sound, no saved game, and nothing made from them.
- Never load, change or delete a saved game or a profile of the player. The
  desktop scripts copy the `Profiles` folder before a run and put it back.
- Never change the registry.
- Never start the game outside `bwvr/tools/desk/desk_run.ps1` in a test.
  Never move or click the real mouse, never bring a window to the front,
  never stop a process that you did not start. A run waits until nobody has
  used the PC for 10 minutes and no game runs.
- Never make a directory link (junction, symlink) to `third_party` or to a
  real folder, and never delete a git worktree folder recursively. Use
  `git worktree remove`. A recursive delete through a junction emptied
  `third_party` one time.
- Never push, publish or upload unless the owner asks for it.

## Design rules

- No motion that the player did not make. No automatic sway, glide or bob,
  not even as a test aid. Verify with still poses and steps.
- The Hand is the real hand, 1:1 in size and place. Never an extended reach.
- Use the game's own way (its packets, states and tests). Find what the
  plain game does with the mouse first.
- Each change of behavior has an ini key with the old behavior as a value,
  and a test key for a live A/B (`test_keys=1`). The owner tests by hotkey.
  Read his words about a symptom literally.
- Hooked functions run many times a frame, and game logic reads back what
  they store. Latch state for each frame.
- The pointer has several readers: window pixels, render pixels, and the
  screen of the game camera.

## Work method

- Build with `.\dev.ps1 build` (it configures, builds and runs the math
  tests). The build is 32 bit. After a change of a header in a build folder
  under a temp path, build with `-Clean`: MSBuild does not track headers
  there.
- Put new math in a pure header with checks in `bwvr/test/controller_math/`.
- Prove a change in the game with a desktop run (`bwvr/tools/desk/`), and
  say plainly in the commit message or the pull request what ran, what did
  not, and what only a headset can show. Do not merge a change of behavior
  that no run has shown.
- Recipes of the scripted controller depend on the render size and on which
  window is in front. Check the first lines of each run log.
- Several agents: one git worktree and one branch for each, land by a fast
  forward, and brief each agent with a file.
- Keep `bwvr/docs/roadmap.md` and the doc of the area current.

## The docs and the comments hold the current state

The roadmap, the README files, the docs and the comments say what is true
now and what is planned. The git history is the history.

- When a task is done or a fault is corrected, remove it from the roadmap
  and from each doc. Do not mark it as done. Write what the mod does now in
  the doc of the area.
- Do not write the story of a change in a doc or a comment: no dates, no
  names of test runs, no "before" and "after", no fault that is gone, no
  design that was dropped, no person who asked for it. Put the reason and
  the proof of a change in the commit message or the pull request.
- A comment says what the code does and why it must be so. It does not say
  what the code did before.
- An open question, a known limit and a thing that only a headset can show
  are the current state. Keep them, and remove each one when it is solved.

## Writing

Comments, log lines, strings that the player sees, docs and commit messages
are in Simplified Technical English (ASD-STE100): short active sentences,
one name for one thing, no contractions. No em dashes. No semicolons in
prose. Match the comment density and the idiom of the code around a change.
