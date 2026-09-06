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
