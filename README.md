# stream-test-cpp

Simple streaming test in c++

## Build

```bash
mkdir build
cd build
cmake ..
make -j
```

## Cli

```bash
SYNOPSIS
        ./test_streams [-x] [-i] [-p <port>] [-h <host>] [-s] [-c] [-n <num>] [-l <length>]

OPTIONS
        -x, --inproc
                    Run in inproc mode

        -i, --ipc   Run in ipc mode
        <port>      Port for connecting with tcp
        <host>      Host for connecting with tcp
        -s, --server
                    run in server mode, cannot be used with "inproc"

        -c, --client
                    run in client mode, cannot be used with "inproc"

        <num>       Number of messages to pass between processes
        <length>    Length of a single message vector to pass
```
