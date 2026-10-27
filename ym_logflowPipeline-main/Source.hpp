#pragma once
#include "Emitter.hpp"

template <typename O>
class Source {
public:
    virtual ~Source() {}
    virtual void produce(Emitter<O>& out) = 0;
};
