# PS5Term development scope

PS5Term is a native terminal homebrew app for an already jailbroken PS5. Its
standalone release contains its terminal interface, built-in file/text commands,
app-owned SSH/SFTP server and optional emulated Linux environment. Users do not
send a separate shell or SSH payload to start those features.

The app does not install a jailbreak. File access remains limited by the process
context provided by the existing homebrew environment. Guest Linux runs under
CPU emulation and shares only the configured PS5 file tree; it is not a native
FreeBSD jail or a hardware-accelerated virtual machine.

The unused privilege-changing helper, generated legacy shell/SSH payload
binaries, standalone virtual-input probe and duplicate stub helper were retired on 2026-11-07. They are not runtime dependencies of the
standalone profile. Historical comparison build scripts and compatibility notes
remain explicitly labeled; dependency preparation still needs the pinned SSH
source archives for the app-owned server's cryptographic libraries.

Input monitors and platform capability experiments are development aids, separate
from shipped feature claims. Keep their actual behavior and firmware limitations
visible. Never substitute host tests or an SDK link result for physical console
acceptance. Firmware 13.60 on the phat is the available hardware test target;
other firmware compatibility remains unverified.

See [feature access](FEATURES.md), [SSH ownership](OWNED_SSH.md), and
[Linux subsystem](SUBSYSTEM.md) for supported behavior and limitations.
