# Host tests

From the repository root, with a C++ compiler supporting sanitizers:

```sh
for name in test_helpers test_pager_time test_ui_battery test_mailbox test_power_policy test_pocsag_receiver test_rssi; do
  c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined "tests/$name.cpp" -o "/tmp/$name" && "/tmp/$name" || exit 1
done
```

After building the firmware once (which installs and patches RadioLib):

```sh
python3 tests/test_receive_bounds.py
python3 tests/test_pocsag_reference.py
```

The bounds test compiles the installed legacy `PagerClient::readData()` with a
deterministic radio test double. The application now uses `pocsag_receiver.h`
instead of that receive path. To reproduce the original overflow, point `--source-dir` at
an **unmodified** RadioLib 5.6.0 `src/protocols/Pager` directory and add
`--expect-overflow`. Do not overwrite the patched firmware dependency for this test.

`test_pocsag_receiver.cpp` covers both address slots in all eight frames,
arbitrary capture alignment, character carry across batches, adjacent pages,
512-character pages, overflow, missing endings, timeouts, BCH repair and error
rejection. It exercises all one-, two- and three-bit corruptions of five
codewords, plus queue interleaving, wrapping and overrun without overwriting
unread data. `test_pocsag_reference.py` uses the actual RadioLib GF encoder as
an independent reference for 50,000 codewords. It also reproduces three legacy
receive defects: unfinished messages reported as successful, merged adjacent
messages, and wrong RIC frame numbering. It also reproduces unread-byte
overwrites, synchronization dropped by reads, and the 256-byte counter wrap in
the legacy capture buffer. `test_rssi.cpp` checks the cached reception peak,
sample age, invalid values, wraparound and preservation during idle polling.

Experimental wake driver (see [test plan](../docs/PREAMBLE-WAKE.md)):

```sh
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/test_power_policy.cpp -o /tmp/test_power_policy
/tmp/test_power_policy
python3 tests/test_power_driver.py
python3 tests/test_direct_reset.py
```

The driver test compiles the actual application header against a radio/GPIO/sleep
test double. It checks the DIO mapping, capture suspension, wake sequencing and
fallback, including a sleep-entry race. The reset test compiles the actual
installed RadioLib reset method. These do not establish over-the-air reliability
or current consumption.
