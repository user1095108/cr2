# cr2
This is my "food-for-thought" repository, as far as basic "hand-rolled" c++ coroutines are concerned. Clearly, the standard c++ coroutines are superior, being stackless (how much stack do you need?), but, on the other hand, these coroutines do not by themselves allocate anything and never throw. This could make them a useful starting point for a custom coroutine implementation.

# build instructions
    git submodule update --init
    g++ -std=c++20 -Ofast corodemo.cpp -o t -levent
