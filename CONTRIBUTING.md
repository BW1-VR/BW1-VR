# How to contribute

Thank you for your help. This file has the rules of the project. They are
short, and most of them come from a lesson that cost time.

## The two roles

- **Testers** play the builds and write reports. A tester needs no tools:
  see "For testers" in the [README](README.md).
- **Contributors** change the code or the documents through pull requests.

## Hard rules

1. **No file of the game in the repo, ever.** No exe, no art, no sound, no
   saved game, no model or texture made from a file of the game. Keep your
   game outside the repo. The folders `local/` and `media/` are not tracked,
   and `bwvr/tools/check_repo.ps1` stops a commit that holds a game file. It
   runs as a git hook (after `.\dev.ps1 setup`) and on GitHub.
2. **Do not share builds or the repo** with a person outside the group.

## How a change gets in

1. Make a branch from `main`. One subject for each branch.
2. Build and test: `.\dev.ps1`. For a change that can affect the game, also
   `.\bwvr\tools\desk\verify_build.ps1 -Tag a`.
3. Open a pull request and fill in its template. GitHub builds it and makes
   an installer, so a tester can try your change before the merge.
4. Buck merges. `main` must always build and pass the tests, because a
   release for the testers is a build of `main`.

Do not push to `main` directly. GitHub does not enforce this on a private
repo of this kind, so it is a rule between us.

## Rules for the mod itself

- **No motion that the player did not make.** No automatic sway, glide or
  bob of the view. When the game moves its camera, the mod cuts or shows the
  scene on a screen. This is the first rule of comfort.
- **The Hand is the real hand, 1:1** in place and size. No extended reach.
  Reach comes from the scale of the world, and from the far grab.
- **A change of behavior has an ini key**, and the old behavior is one of
  its values. Give a test key for a live A/B when the change is about feel
  (`test_keys=1`). Check which keys are free first.
- **Use the game's own way** where it has one: its packets, its states, its
  tests. Find what the plain game does with the mouse, then give the game
  the same thing from the hand. Do not copy logic of the game into the mod.
- **Hooked functions run many times a frame.** Latch what belongs to a
  frame.
- **The log tells the story.** One line for each change of state, with the
  numbers, never a line for each frame. A log from a tester's session must
  show what happened in the case that he did not like.
- **Say what is proven.** In a pull request and in the docs, say what a
  desktop run showed, what only a headset can show, and what did not run.

## Tests

| Test | Needs | How |
| --- | --- | --- |
| The math tests | Nothing | Part of `.\dev.ps1 build`. Put new math in a pure header and add checks in `bwvr/test/controller_math/` |
| Desktop runs | The game | `bwvr/tools/desk/`, see its README. A scripted controller plays, and the script reads the log |
| The headset | A headset | The only proof of feel and comfort. Say which headset and runtime |

The desktop runs never move the real mouse, never bring a window to the
front, and put your settings, your profile and your saved games back after
each run. Keep those rules in each new script.

## Style

- **C++**: follow the code around your change: its names, its comment
  density, its idiom. C++17, 32 bit, static CRT, warnings at `/W4`.
- **Text** (comments, log lines, strings that the player sees, docs, commit
  messages): short, plain, active sentences. One name for one thing. The
  docs use Simplified Technical English, because readers of many languages
  use them. No em dashes.
- **Docs**: each area has one doc in `bwvr/docs/`. When the code changes,
  the doc says what the code does now. `[V]` marks a fact that was verified
  in the disassembly.
- **Commits**: an imperative subject line that says what changes ("Keep the
  Hand at the real hand in the Totem state"), then why, if it is not plain.

## Reverse engineering

The notes are in `bwvr/docs/` (start with `hand.md`, "How to read this
file" and "Tools used"). The addresses of the game are in
`bwvr/src/common/bw_addresses.h`. The mod fits one exe only, the v1.42 file,
and it checks the SHA-1 before it hooks anything.

Two tools read the code of the game. Both use the function names of the
public project `bw1-decomp` (see `third_party/README.md`).

- `python bwvr\tools\disasm.py 0x005B3C70` gives the disassembly of one
  function in less than a second, with the names. It also finds a name
  (`--find`), the callers of a function (`--callers`) and the uses of an
  address (`--xref`). It needs Python 3 and `pip install capstone`.
- `.\bwvr\tools\re.ps1 decompile 0x005B3C70` gives the C of a function from
  Ghidra, in `local\ghidra\out\`. It needs Ghidra 12 and a JDK 21.

Set them up one time with `.\bwvr\tools\re.ps1 setup`. It gets the names,
and it makes a Ghidra project of your `runblack.exe` in `local\ghidra`
(about 3 minutes). `.\bwvr\tools\re.ps1 status` says what is there. The
project is large and holds the game, so it stays on your machine: git does
not track `local`. You can open it in Ghidra too
(`local\ghidra\project\runblack.gpr`). Close it there before you run
`re.ps1 decompile`, because a Ghidra project takes one user at a time.

A name or a signature from the symbol file is a claim, not a proof. The exe
has no symbols: a person or a tool wrote each Windows name, and Ghidra makes
the parameter types of a function from it. Check a type in the code before
you use it. When a name is wrong, put the right one in
`bwvr/ghidra/name-corrections.txt` with the proof. Both tools read that
file. `python bwvr\tools\check_names.py` compares the names with the Mac
symbols of the same project, which are from the compiler.

## AI tools

Most of this project is written with AI coding tools, and you are welcome to
use them too. [`CLAUDE.md`](CLAUDE.md) holds the rules for them. You are
responsible for what you submit: build it, run it, and read it first.

## License of your work

The project has the MIT license. When you open a pull request, you agree
that your contribution has the same license.
