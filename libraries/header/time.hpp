#pragma once

#include <chrono>

using Clock = std::chrono::high_resolution_clock::time_point;

Clock begin();
double end(Clock start);
double calculateFPS(double dt);