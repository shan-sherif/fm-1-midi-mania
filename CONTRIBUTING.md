# Contributing

Issues and pull requests are welcome in this repository.

- **Bugs**: one issue per bug, with steps to reproduce it, what you expected and what happened. Say whether it
  happens on the FM-1 or in a host build (desktop, browser).
- **Changes**: keep each pull request to one thing. Run the tests that apply (`tests/run_tests.sh`, and for the game
  `tests/judge_test.c`, see [docs/BUILDING_WINDOWS.md](docs/BUILDING_WINDOWS.md)).
- **Size**: the FM-1's flash is small (see the README's flash budget). Say what a change adds to the image.
- **Licence**: contributions are GPL-3.0-only, like the rest. Keep the SPDX header on every file. Code in
  `firmware/engines/` stays MIT and code in `firmware/bsp/` stays MPL-2.0.

Problems in this firmware belong here, not with Felucca: it is a modified version, and Felucca's maintainers are not
responsible for it. A bug that is also in Felucca (check it in the reference copy, [README.md](README.md#based-on-felucca)) can be
reported to [Felucca](https://github.com/hugelton/Felucca/issues) too.
