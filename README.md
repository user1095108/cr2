# cr2
This is a "food-for-thought" repository, as far as basic "hand-rolled" c++ coroutines are concerned. Clearly, the standard c++ coroutines are superior, being stackless (how much stack do you need?), but, on the other hand, these coroutines do not by themselves allocate anything and never throw. This could make them a useful starting point for a custom coroutine implementation.

Note that asynchronous programming is not synonimous with coroutines.

# build instructions
    git submodule update --init
    g++ -std=c++20 -Ofast corodemo.cpp -o t -levent
# resources
* [Asynchronous I/O and event notification on linux](http://davmac.org/davpage/linux/async-io.html)
* [Asynchronous Programming Under Linux](https://unixism.net/loti/async_intro.html)
* [libevent](https://libevent.org/)
* [libev](https://github.com/enki/libev)
* [libeio](http://software.schmorp.de/pkg/libeio.html)
* [libuv](https://github.com/libuv/libuv)
