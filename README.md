# tii
A terminal/text interface for [ii](http://git.suckless.org/ii)

Dependencies:
[C89](https://en.wikipedia.org/wiki/ANSI_C#C89),
[posix.1 2001](https://en.wikipedia.org/wiki/POSIX#POSIX.1-2001),
[ctrl caret notation](https://en.wikipedia.org/wiki/Caret_notation),
[ii](http://git.suckless.org/ii).

Usage:
`cd` into the `irc` directory and launch `tii`.
The `out` file will be shown in terminal, along with a channel list
and an input line at the bottom.

    ^L next channel
    ^H prev channel
    ^N next server
    ^P prev server
    ^U clear input
    ^C exit tii
