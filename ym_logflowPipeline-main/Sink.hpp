#pragma once

template <typename I>
class Sink {
public:
    virtual ~Sink() {}
    virtual void open() {}
    virtual void consume(const I& item) = 0;
    virtual void close() {}
};

