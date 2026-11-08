#!/usr/bin/env bash
# PS5Term - focused host checks. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
output=${TEST_OUTPUT_DIR:-build/tests}
mkdir -p "$output"
link_flags=(-Wl,--gc-sections)
if [[ $(uname -s) == Darwin ]]; then link_flags=(-Wl,-dead_strip); fi
cxx=${HOST_CXX:-clang++}
flags=(-std=c++20 -Wall -Wextra -Werror -Isrc)
"$cxx" "${flags[@]}" -D__PROSPERO__ tests/terminal/test_pty_policy.cpp src/platform/pty_launcher.cpp -o "$output"/pty-policy
"$cxx" "${flags[@]}" -DPS5TERM_LINUX_SUBSYSTEM -DPS5TERM_GUEST_MMAP_TEST -Ibackend/subsystem tests/terminal/test_linux_subsystem.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp -o "$output"/linux-loader
"$cxx" "${flags[@]}" -DPS5TERM_LINUX_SUBSYSTEM -DPS5TERM_LINUX_PERSISTENCE -DPS5TERM_GUEST_MMAP_TEST -Ibackend/subsystem tests/terminal/test_linux_subsystem.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/platform/linux_data_disk.cpp -o "$output"/linux-loader-data
"$cxx" "${flags[@]}" -DPS5TERM_LINUX_SUBSYSTEM -DPS5TERM_LINUX_PERSISTENCE -DPS5TERM_LINUX_SHARED_FOLDER -DPS5TERM_GUEST_MMAP_TEST -Ibackend/subsystem tests/terminal/test_linux_subsystem.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/platform/linux_data_disk.cpp src/platform/linux_shared_folder.cpp src/platform/directory.cpp -o "$output"/linux-loader-share
"$output"/linux-loader-share
"$cxx" "${flags[@]}" -DPS5TERM_LINUX_SUBSYSTEM -DPS5TERM_LINUX_PERSISTENCE -DPS5TERM_LINUX_SHARED_FOLDER -DPS5TERM_LINUX_NETWORK -DPS5TERM_GUEST_MMAP_TEST -Ibackend/subsystem tests/terminal/test_linux_subsystem.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/platform/linux_data_disk.cpp src/platform/linux_shared_folder.cpp src/platform/directory.cpp -o "$output"/linux-loader-network
"$output"/linux-loader-network
"$cxx" "${flags[@]}" -Ibackend/subsystem tests/terminal/test_linux_shared_folder.cpp src/platform/linux_shared_folder.cpp src/platform/directory.cpp -o "$output"/linux-shared-folder
"$output"/linux-shared-folder
"$output"/linux-loader-data
"$cxx" "${flags[@]}" -DPS5TERM_NATIVE_MEMORY_TEST -Ibackend/subsystem tests/terminal/test_guest_memory.cpp -o "$output"/guest-memory
"$cxx" "${flags[@]}" -Ibackend/subsystem tests/terminal/test_linux_data_disk.cpp src/platform/linux_data_disk.cpp -o "$output"/linux-data-disk
"$cxx" "${flags[@]}" tests/terminal/test_linux_disk_expansion.cpp src/platform/linux_data_disk.cpp -o "$output"/linux-disk-expansion
"$cxx" "${flags[@]}" -DPS5TERM_DATA_NO_OPENAT tests/terminal/test_linux_disk_expansion.cpp src/platform/linux_data_disk.cpp -o "$output"/linux-disk-expansion-native
"$output"/linux-disk-expansion-native
"$output"/linux-disk-expansion
"$output"/linux-data-disk
"$cxx" "${flags[@]}" -DPS5TERM_DATA_NO_OPENAT tests/terminal/test_linux_data_disk.cpp src/platform/linux_data_disk.cpp -o "$output"/linux-data-disk-native-path
"$output"/linux-data-disk-native-path
"$output"/guest-memory
"$output"/linux-loader
"$output"/pty-policy
"$cxx" "${flags[@]}" tests/terminal/test_ssh_command_session.cpp src/ssh/command_session.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp -o "$output"/ssh-command-session
"$cxx" "${flags[@]}" -D__PROSPERO__ -Itests/fakes/ps5-directory tests/terminal/test_native_directory.cpp src/platform/directory.cpp -o "$output"/native-directory
"$cxx" "${flags[@]}" tests/terminal/test_directory_records.cpp src/platform/directory.cpp -o "$output"/directory-records
"$cxx" "${flags[@]}" tests/terminal/test_disk_usage.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp -o "$output"/disk-usage
"$cxx" "${flags[@]}" tests/terminal/test_filename_expansion.cpp src/platform/filename_expansion.cpp -o "$output"/filename-expansion
"$cxx" "${flags[@]}" tests/terminal/test_system_info.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp -o "$output"/system-info
"$cxx" "${flags[@]}" tests/terminal/test_guest_agent_close.cpp src/platform/guest_agent.cpp -o "$output"/guest-agent-close
"$output"/guest-agent-close
"$cxx" "${flags[@]}" tests/terminal/test_keymap.cpp src/keymap.cpp -o "$output"/keymap
"$output"/keymap
"$output"/system-info
"$output"/filename-expansion
"$output"/disk-usage
"$cxx" "${flags[@]}" tests/terminal/test_builtin_shell.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp -o "$output"/builtin-shell
"$cxx" "${flags[@]}" tests/terminal/test_native_layout.cpp tooling/native/sce_module_writer.cpp -o "$output"/native-layout
python3 tools/verify-vterm.py
mkdir -p "$output/vterm-objects"
vterm_objects=()
for source in third_party/libvterm/src/*.c; do
    object="$output/vterm-objects/$(basename "$source" .c).o"
    "${HOST_CC:-clang}" -std=c11 -O2 -Ithird_party/libvterm/include -c "$source" -o "$object"
    vterm_objects+=("$object")
done
"${HOST_CC:-clang}" -std=c11 -O2 -Wall -Wextra -Werror -c src/terminal_memory.c -o "$output/vterm-objects/terminal_memory.o"
vterm_objects+=("$output/vterm-objects/terminal_memory.o")
"${HOST_CC:-clang}" -std=c11 -O2 -Wall -Wextra -Isrc -c src/platform/pty.c -o "$output/vterm-objects/pty.o"
platform_c_objects=("$output/vterm-objects/pty.o")
"$cxx" "${flags[@]}" tests/terminal/test_builtin_connection.cpp src/connection.cpp src/terminal_transport.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp src/platform/launcher.cpp src/platform/pty_launcher.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/terminal.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" "${platform_c_objects[@]}" -o "$output"/builtin-connection
"$cxx" "${flags[@]}" tests/terminal/test_terminal_transport.cpp src/terminal_transport.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" -o "$output"/terminal-transport
"$output"/terminal-transport
"$cxx" "${flags[@]}" tests/terminal/test_terminal_search.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" -o "$output"/terminal-search
"$output"/terminal-search
"$cxx" "${flags[@]}" tests/terminal/test_terminal_model.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" -o "$output"/terminal-model
"$cxx" "${flags[@]}" tests/terminal/test_scrollback.cpp src/scrollback.cpp -o "$output"/scrollback
"$cxx" "${flags[@]}" tests/terminal/test_terminal.cpp src/terminal.cpp src/telnet.cpp -o "$output"/terminal
"$cxx" "${flags[@]}" tests/terminal/test_pointer.cpp src/pointer.cpp src/terminal.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" -o "$output"/pointer
"$cxx" "${flags[@]}" tests/terminal/test_settings.cpp src/settings.cpp -o "$output"/settings
"$cxx" "${flags[@]}" tests/terminal/test_ssh_service.cpp src/ssh_service.cpp -o "$output"/ssh-service
"${HOST_CC:-clang}" -Wall -Wextra -Werror -Ibackend tests/terminal/test_parse.c -o "$output"/parser
"$cxx" "${flags[@]}" -ffunction-sections -fdata-sections tests/terminal/test_controller.cpp src/app.cpp src/terminal.cpp src/telnet.cpp src/connection.cpp src/terminal_transport.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp src/platform/launcher.cpp src/platform/pty_launcher.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/ssh_service.cpp src/settings.cpp src/pointer.cpp src/diagnostics.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" "${platform_c_objects[@]}" "${link_flags[@]}" -o "$output"/controller
"$cxx" "${flags[@]}" -ffunction-sections -fdata-sections tests/terminal/test_app_vt.cpp src/app.cpp src/terminal.cpp src/telnet.cpp src/connection.cpp src/terminal_transport.cpp src/builtin_shell.cpp src/platform/system_info.cpp src/platform/filename_expansion.cpp src/platform/disk_usage.cpp src/platform/directory.cpp src/platform/launcher.cpp src/platform/pty_launcher.cpp src/platform/linux_subsystem.cpp src/platform/guest_agent.cpp src/ssh_service.cpp src/settings.cpp src/pointer.cpp src/diagnostics.cpp src/terminal_model.cpp src/scrollback.cpp "${vterm_objects[@]}" "${platform_c_objects[@]}" "${link_flags[@]}" -o "$output"/app-vt
"$output"/app-vt
"$cxx" "${flags[@]}" tests/terminal/test_diagnostics.cpp src/diagnostics.cpp -o "$output"/diagnostics
"$cxx" "${flags[@]}" tests/terminal/test_capabilities.cpp src/platform/probe.cpp src/platform/capabilities.cpp -o "$output"/capabilities
"$cxx" "${flags[@]}" -Ibackend tests/terminal/test_broker_protocol.cpp backend/broker/protocol.cpp -o "$output"/broker
"$cxx" "${flags[@]}" -Ibackend tests/terminal/test_session.cpp src/session.cpp src/platform/launcher.cpp -o "$output"/session
"${HOST_CC:-clang}" -Wall -Wextra -Werror -I. tests/terminal/test_safe_launch.c -o "$output"/safe-launch
"$cxx" "${flags[@]}" -DPS5TERM_HOST_NATIVE_PROBE_TEST -DPS5TERM_HOST_CHILD_PROBE_TEST tests/terminal/test_native_probe.cpp src/platform/native_probe.cpp src/platform/native_child.cpp src/platform/probe.cpp src/platform/capabilities.cpp -o "$output"/native-probe
"$cxx" "${flags[@]}" tests/terminal/test_native_child.cpp src/platform/native_child.cpp -o "$output"/native-child
"$cxx" "${flags[@]}" tests/terminal/test_password_entry.cpp -o "$output"/password-entry
for test in password-entry ssh-command-session native-directory directory-records builtin-shell builtin-connection native-layout terminal-model scrollback native-child native-probe broker session safe-launch capabilities diagnostics terminal pointer settings ssh-service parser controller; do
    "$output/$test"
done
