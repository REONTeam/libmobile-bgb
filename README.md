libmobile for BGB
=================

This is one of the software implementations of [libmobile](https://github.com/REONTeam/libmobile), interfacing with the [BGB](http://bgb.bircd.org/) emulator.

It provides BGB with the ability to emulate the [Mobile Adapter GB](https://bulbapedia.bulbagarden.net/wiki/Mobile_Game_Boy_Adapter) and connect games with homebrew servers.


How to use
----------

On linux/mac, all you need is `make` and `gcc` or `clang`, then run `make` to build the program.  
On windows, you will need [msys2](https://www.msys2.org/) and the following packages: `mingw-w64-x86_64-gcc make`. Once installed, run `./make-mingw`.

Once you have built the program, open up the BGB emulator, select "Link-\>Listen" in its menu, and then fire up the program by running `./mobile`.
