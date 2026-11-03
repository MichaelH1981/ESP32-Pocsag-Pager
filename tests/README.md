# Host tests

From the repository root, with a C++ compiler supporting sanitizers:

```sh
for name in test_helpers test_pager_time test_ui_battery test_mailbox; do
  c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined "tests/$name.cpp" -o "/tmp/$name" && "/tmp/$name" || exit 1
done
```

After building the firmware once (which installs and patches RadioLib):

```sh
python3 tests/test_receive_bounds.py
```

This compiles the actual installed `PagerClient::readData()` with a deterministic
radio test double. To reproduce the original overflow, point `--source-dir` at
an **unmodified** RadioLib 5.6.0 `src/protocols/Pager` directory and add
`--expect-overflow`. Do not overwrite the patched firmware dependency for this test.

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
