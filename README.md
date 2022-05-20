# cr2
This is my "food-for-thought" repository, as far as basic "hand-rolled" c++ coroutines are concerned. Clearly, the standard c++ coroutines are superior, being stackless (how much stack do you need?), but, on the other hand, these coroutines do not by themselves allocate anything and their stacks can be allocated out of their parent thread's stack.

# build instructions
    git submodule update --init
    g++ -std=c++20 -Ofast corodemo.cpp -o t -levent
