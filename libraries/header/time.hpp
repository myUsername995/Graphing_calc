#pragma once

#include <chrono>

// Abstraction for the user
using Clock = std::chrono::high_resolution_clock::time_point;

Clock begin();
double end(Clock start);
double calculateFPS(double dt);