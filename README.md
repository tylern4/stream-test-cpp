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

        <num>       Comma-separated list of message counts (e.g. 100,1000,10000)
        <length>    Comma-separated list of message vector lengths (e.g. 100,1000,10000)

The client runs every length x num combination in a loop, prints progress
(`[i/total]`) and reports round-trip time plus estimated MB/s for each test.
Pass `-o` to kill the server after all tests complete.
```
