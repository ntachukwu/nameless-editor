## Entering raw mode

Use termios to handle the variouse flags needed to disable to enter raw mode.

## Preparing the text editor

- Learning about the VT100 digital screen
- The console is STDOUT_FILENO, writing to it uses the _write_ function. All these are from the _unistd.h_ module.
