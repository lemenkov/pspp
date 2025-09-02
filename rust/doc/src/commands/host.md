# HOST

In the syntax below, the square brackets must be included in the command
syntax and do not indicate that that their contents are optional.

```
HOST COMMAND=['COMMAND'...]
     TIMELIMIT=SECS.
```

`HOST` executes one or more commands, each provided as a string in
the required `COMMAND` subcommand, in the shell of the underlying
operating system.  PSPP runs each command in a separate shell process
and waits for it to finish before running the next one.  If a command
fails (with a nonzero exit status, or because it is killed by a signal),
then PSPP does not run any remaining commands.

PSPP provides `/dev/null` as the shell's standard input.  If a
process needs to read from stdin, redirect from a file or device, or use
a pipe.

PSPP displays the shell's standard output and standard error as PSPP
output.  Redirect to a file or `/dev/null` or another device if this is
not desired.

By default, PSPP waits as long as necessary for the series of
commands to complete.  Use the optional `TIMELIMIT` subcommand to limit
the execution time to the specified number of seconds.

PSPP built for mingw does not support all the features of `HOST`.

PSPP rejects this command if the [`SAFER`](set.md#safer) setting is
active.

## Example

The following example runs `rsync` to copy a file from a remote
server to the local file `data.txt`, writing `rsync`'s own output to
`rsync-log.txt`.  PSPP displays the command's error output, if any.  If
`rsync` needs to prompt the user (e.g. to obtain a password), the
command fails.  Only if the `rsync` succeeds, PSPP then runs the
`sha512sum` command.

```
HOST COMMAND=['rsync remote:data.txt data.txt > rsync-log.txt'
              'sha512sum -c data.txt.sha512sum].
```

