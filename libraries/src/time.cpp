/*
A library that you can use to benchmark things and do other stuff.

How to use: use an expression like this at the start: Clock clk = begin();
and then use an expression like this at the end: double time = end(clk);
It will always return in milliseconds.

CalculateFPS: You can call the calculateFPS function with a time that you measured between the start of one frame and the end of one frame 
(dt), and it will give you back an FPS count. If you pass more values the FPS value becomes more accurate, because each dt is saved in an 
array (up until a certain size), and they're averaged.
*/

#include "time.hpp"

// The higher the size -> the less reactive the counter is to big changes in dt, but also more stable
#define sampleSize 120

double dts[sampleSize];             // Keep track of the last few frames
int track = 0;
bool hasWrappedAround = false;

Clock begin() {
    return std::chrono::high_resolution_clock::now();
}

double end(Clock start) {
    Clock finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = finish - start;
    return diff.count();
}

double calculateFPS(double dt){
    dts[track++] = dt;

    // Wrap it around, so that you replace the oldest frames with the newer ones
    if (track >= sampleSize){
        track = 0; hasWrappedAround = true;
    }

    int size = hasWrappedAround ? sampleSize : track;

    double totalDt = 0;
    for (int i = 0; i < size; i++){
        totalDt += dts[i];
    }
    double avgDt = (totalDt / (double)size) / 1000.0;

    return 1.0 / avgDt;
}