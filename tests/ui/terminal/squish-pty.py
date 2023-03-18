#! /usr/bin/python3
import os
import pty
import signal
import sys

def main(args):
    if len(args) < 2:
        sys.stderr.write('''\
usage: squish-pty COMMAND [ARG]...
Squishes both stdin and stdout into a single pseudoterminal and
passes it as stdin and stdout to the specified COMMAND.
''')
        return 1

    status = pty.spawn(args[1:])
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    elif os.WIFSIGNALED(status):
        signal.raise_signal(os.WTERMSIG(status))
    else:
        assert False

if __name__ == '__main__':
    sys.exit(main(sys.argv))
    
