#!/bin/bash
# Helpers for driving a Haiku GUI application with "hey" from a test script.
# Source this file on the Haiku machine:  . hey-lib.sh; HEY_APP=application/x-vnd.Foo-Bar
#
# hey prints replies like:
#    "result" (B_INT32_TYPE) : 1 (0x00000001)
#    "result" (B_STRING_TYPE) : "text"
#    "result" (B_BOOL_TYPE) : TRUE
# hey_result normalizes that to 1 / text / 1.
HEY_APP=${HEY_APP:-}

hey_result() {
	sed -n 's/.*"result" ([A-Z_0-9]*) : //p' | head -1 \
		| sed 's/^"\(.*\)"$/\1/; s/ (0x[-0-9a-fA-F]*)$//; s/^TRUE$/1/; s/^FALSE$/0/'
}

# hey_get <view name> [window name or index]   -> "Value" property
# (BTextControl exposes its text as Value; BControl/BCheckBox as int32.)
hey_get() {
	hey "$HEY_APP" get Value of View "$1" of Window "${2:-0}" | hey_result
}

# hey_get_prop <property> <view name> [window]   e.g. hey_get_prop Enabled ok
hey_get_prop() {
	hey "$HEY_APP" get "$1" of View "$2" of Window "${3:-0}" | hey_result
}

# hey_set <view name> <value> [window]
# Strings must reach hey quoted, otherwise a value starting with "file" is
# parsed as a file reference and numbers are sent as int32.
hey_set() {
	local value="$2"
	case "$value" in
		''|*[!0-9]*) value="\"$value\"" ;;
	esac
	hey "$HEY_APP" set Value of View "$1" of Window "${3:-0}" to "$value" >/dev/null
}

# hey_count_items <list view name> [window]   -> number of BListView items
hey_count_items() {
	hey "$HEY_APP" count Item of View "$1" of Window "${2:-0}" | hey_result
}

# hey_select <list view name> <index> [window]  -> like a click (sends the
# selection message; BListView::Select() does that for programmatic selects)
hey_select() {
	hey "$HEY_APP" set Selection "$2" of View "$1" of Window "${3:-0}" to "bool(true)" >/dev/null
}

# hey_invoke_item <list view name> <index> [window] -> select + invoke (double click)
hey_invoke_item() {
	hey "$HEY_APP" do Item "$2" of View "$1" of Window "${3:-0}" >/dev/null
}

# hey_send <what code> [window]  -> send a custom message ('mnt ', 'addf'...)
# to the application (no window) or to a window. The receiver must reply
# (message->IsSourceWaiting() -> SendReply(B_REPLY)) or hey waits forever.
hey_send() {
	if [ -n "${2:-}" ]; then
		hey "$HEY_APP" "$1" of Window "$2" >/dev/null
	else
		hey "$HEY_APP" "$1" >/dev/null
	fi
}

# Extra fields: hey APP what of Window 0 with which=int32(1) and name="x"

# wait_window <title>     waits until a window thread "w><title>" exists
wait_window() { waitfor "w>$1"; }
# wait_window_gone <title>
wait_window_gone() { waitfor -e "w>$1"; }
# wait_app_gone <executable name>  (main thread name = executable name)
wait_app_gone() { waitfor -e "$1"; }

# mounted_count <fs short name>  (Haiku's "mount" without arguments prints usage; df lists volumes with their type)
mounted_count() { df 2>/dev/null | grep -c " $1 "; }

# take_screenshot <file.png>  (kill the screen saver first, it blanks the shot)
take_screenshot() {
	kill $(roster 2>/dev/null | grep -i screen_blanker | awk '{print $1}') 2>/dev/null
	screenshot -s -f png "$1"
}
