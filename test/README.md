# test/

Native-env unit tests (M2): line-bus framing + dispatch, agent-state
snapshot parsing and persona derivation, with `agentStateRandom()` supplied
by a seeded PRNG. `lib/agent-state` and `lib/line-bus` are Arduino-free by
design so they compile under PlatformIO's `native` platform.
