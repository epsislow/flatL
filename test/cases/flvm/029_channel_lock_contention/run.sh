#!/bin/sh
set -eu
root=$1
tmp=$2
case_dir="$root/test/cases/flvm/029_channel_lock_contention"
"$root/bin/flc" -o "$tmp/channel_owner.fb" "$case_dir/owner.fl"
"$root/bin/flc" -o "$tmp/channel_writer.fb" "$case_dir/writer.fl"
"$root/bin/flc" -o "$tmp/channel_reader.fb" "$case_dir/reader.fl"
"$root/bin/flc" -o "$tmp/channel_locker.fb" "$case_dir/waiter.fl"
"$root/bin/flc" -o "$tmp/channel_fail_owner.fb" "$case_dir/fail_owner.fl"
"$root/bin/flc" -o "$tmp/channel_lock_while_holding.fb" "$case_dir/lock_while_holding.fl"
"$root/bin/flc" -o "$tmp/channel_set_while_holding.fb" "$case_dir/set_while_holding.fl"
"$root/bin/flc" -o "$tmp/channel_var_while_holding.fb" "$case_dir/var_while_holding.fl"
"$root/bin/flc" -o "$tmp/channel_waiter_two.fb" "$case_dir/waiter_channel_two.fl"
"$root/bin/flc" -o "$tmp/channel_waiter_fail_after_lock.fb" "$case_dir/waiter_fail_after_lock.fl"
make -s -C "$root" test-channel-lock-contention TEST_BIN="$tmp/channel_lock_contention_test"
"$tmp/channel_lock_contention_test" "$tmp/channel_owner.fb" "$tmp/channel_writer.fb" "$tmp/channel_reader.fb" "$tmp/channel_locker.fb" "$tmp/channel_fail_owner.fb" "$tmp/channel_lock_while_holding.fb" "$tmp/channel_set_while_holding.fb" "$tmp/channel_var_while_holding.fb" "$tmp/channel_waiter_two.fb" "$tmp/channel_waiter_fail_after_lock.fb"
