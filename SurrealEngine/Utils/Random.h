#pragma once
#include <random>

// Reproducible, isolated diagnostic fixtures; normal gameplay never constructs this.
class ScopedRandomSeed
{
public:
    explicit ScopedRandomSeed(unsigned seed);
    ~ScopedRandomSeed();
    ScopedRandomSeed(const ScopedRandomSeed&) = delete;
    ScopedRandomSeed& operator=(const ScopedRandomSeed&) = delete;
private:
    std::default_random_engine saved;
};

int RandInt(int Min, int Max); // A random integer value between [Min, Max]
int RandInt(int Max); // A random integer value between [0, Max]
float FRand(); // A random float value between [0, 1]
float FRandRange(float Min, float Max);
