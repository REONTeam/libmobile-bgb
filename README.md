libmobile for BGB
=================

This is one of the software implementations of [libmobile](https://github.com/REONTeam/libmobile), interfacing with the [BGB](http://bgb.bircd.org/) emulator.

It provides BGB with the ability to emulate the [Mobile Adapter GB](https://bulbapedia.bulbagarden.net/wiki/Mobile_Game_Boy_Adapter) and connect games with homebrew servers.


Usage
-----

Open up the BGB emulator, select "Link-\>Listen" in its right click menu, and then fire up the program by running `./mobile` (or `./mobile.exe` on windows).

The mobile adapter has two means of communication, it can either directly call a different phone number and communicate over the telephone line, in P2P (peer-to-peer, or player-to-player) fashion, or it can instead call an ISP (Internet Service Provider) to connect to servers on the internet. As a general rule of thumb, any feature that communicates directly with another player will use the P2P mode, and will not require any prior configuration to function. The primary use for the P2P mode are direct battles and trades in Pocket Monsters: Crystal Version. Practically every other game and functionality connects to internet servers, and requires the user configure their account through the use of the Mobile Trainer cartridge.

By default, when dialing a number through a game's P2P functionality, the emulator will interpret any 12-digit phone number as an IP address. This works by splitting the 12 digits into chunks of three, which form the four components of an IP address. For example, '127000000001' is the number corresponding to the IP address '127.0.0.1'. The emulator will attempt to establish a TCP connection to that IP address, on port 1027. The receiving party must be reachable on the specified port, and have selected the listening option in-game, prior to receiving the call.

To avoid complicated matters with regards to opening TCP ports, an alernative communication method is provided through the use of a [relay server](https://github.com/REONTeam/mobile-relay/). By configuring one such server's IP address through the `--relay` option, each user will be assigned a phone number upon attempting to use any P2P functionality, which they may use to dial eachother. A secret key will be stored in config.bin, which will be used to preserve the assigned phone number across restarts of the emulator.

For the emulator to reach servers on the internet, a DNS server must be configured. This is done through the `--dns1` and/or `--dns2` options. The system's DNS resolver is avoided because all of the original game servers are unreachable on the open internet. The exact IP addresses to configure here will depend on the third-party game server that may be utilized.


Compilation
-----------

To compile the git sources, the following packages must be installed: `gcc`, `make`, `autoconf`, `automake`, `libtool`. Once installed, execute the following commands:

```
autoreconf -vfi
mkdir build
cd build
../configure
make
```

On windows, you will need a Unix environment, such as [msys2](https://www.msys2.org/). The currently recommended package to install to provide `gcc` is `mingw-w64-x86_64-gcc`. One should use the MINGW64 environment to use it.

Alternatively, a `meson` build is also provided. See its [quickstart guide](https://mesonbuild.com/Quick-guide.html) for more information.
